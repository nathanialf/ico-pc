/*
 * port/platform/backup.h
 *
 * Options > Extras > Back up your data and Restore from a backup: the
 * player's data in the user folder (the memory card folders memcard*, the
 * save states under states/, config.toml and achievements.toml) as one zip
 * with a manifest, backup.txt, and such a zip put back. Pure C over
 * host_fs.h and miniz: no SDL, no game, no log lines (the caller logs from
 * the report). backup_host.c runs it on a worker, asks the system for the
 * file and restarts the game; the backup_test ctest runs it on a folder of
 * its own.
 *
 * Never in a backup: the logs, ico-pc.ini (it names this device's disc
 * image and folders), the pipeline caches, the dumps, the extracted game
 * data, the texture and model packs, a save state's developer dumps
 * (states/<slug>/dump-*) and files still being written (*.tmp, *.part).
 *
 * A restore is two steps. ico_backup_restore checks the zip and unpacks it
 * into ICO_BACKUP_STAGING_PART in the user folder, then renames that to
 * ICO_BACKUP_STAGING: nothing the game uses is touched. The next start
 * (main_host.c, before the settings, the card or the achievements are
 * read) calls ico_backup_apply_pending, which moves each thing the backup
 * holds into place: the thing there now renamed to <name>.old-<time>, the
 * backup's moved in, and the old copies deleted once every move worked; a
 * move that fails puts every earlier one back, so the data is as it was,
 * and a thing whose move back fails too is named with where the player's
 * own copy is (IcoBackupReport.left); the old copies an earlier start left
 * are named at every start (IcoBackupReport.stale).
 * Doing the moves at the start rather than at once means nothing the
 * running game still writes on its way out (the settings saved on quit,
 * ui_settings_save_on_quit; the achievements' writer) can land over the
 * restored files.
 */
#ifndef ICO_PLATFORM_BACKUP_H
#define ICO_PLATFORM_BACKUP_H

#include <stddef.h>

/* The manifest: its name in the zip, its first line's words and the
   format this code writes and reads (a higher one is refused as NEWER). */
#define ICO_BACKUP_MANIFEST "backup.txt"
#define ICO_BACKUP_MAGIC "ICO PC backup"
#define ICO_BACKUP_FORMAT 1

/* The bounds a zip must keep to be read: entries (the manifest not
   counted), one file's size and all files' size, uncompressed. Four save
   states of each of seven discs with their pictures and the cards come to
   about 1 GB at the most. */
#define ICO_BACKUP_MAX_FILES 8192
#define ICO_BACKUP_MAX_FILE_BYTES (512ull << 20)
#define ICO_BACKUP_MAX_BYTES (3ull << 30)
/* the manifest's own size bound */
#define ICO_BACKUP_MAX_MANIFEST 4096

/* In the user folder: a restore being unpacked, and one unpacked and ready
   for the next start. */
#define ICO_BACKUP_STAGING_PART "backup-restore.part"
#define ICO_BACKUP_STAGING "backup-restore"

typedef enum IcoBackupResult {
    ICO_BACKUP_OK = 0,
    ICO_BACKUP_NONE,       /* apply: no restore waits */
    ICO_BACKUP_NOTHING,    /* write: the user folder holds nothing to back up */
    ICO_BACKUP_BUSY,       /* the memory card, a save state or the disc is being written or read */
    ICO_BACKUP_READ,       /* a file (or the zip) could not be read */
    ICO_BACKUP_WRITE,      /* the zip, the unpacked files or a move could not be written */
    ICO_BACKUP_NOT_BACKUP, /* not a zip, or a zip without this program's manifest */
    ICO_BACKUP_NEWER,      /* the manifest's format is newer than ICO_BACKUP_FORMAT */
    ICO_BACKUP_DAMAGED,    /* the entries disagree with the manifest, or an entry's data is bad */
    ICO_BACKUP_UNSAFE,     /* an entry's name is outside the backup's folders */
    ICO_BACKUP_TOO_BIG,    /* past a bound above */
} IcoBackupResult;

/* The log's words for a result ("ok", "not a backup", ...). */
const char *ico_backup_result_text(IcoBackupResult r);

/* What a backup records about where it was made. */
typedef struct IcoBackupInfo {
    const char *version;  /* the program's version label, "" for a development build */
    const char *commit;   /* the build's commit */
    const char *platform; /* state_file.h ico_state_platform_tag */
    const char *disc;     /* the running disc's slug */
    long long time;       /* seconds since 1970 */
} IcoBackupInfo;

/* A manifest as read (ico_backup_inspect, ico_backup_apply_pending). */
typedef struct IcoBackupManifest {
    int format;
    char version[32];
    char commit[48];
    char platform[32];
    char disc[16];  /* the disc played when it was made */
    char discs[96]; /* that disc and every disc with save states, spaces between */
    long long time;
    int files;                /* entries but the manifest */
    unsigned long long bytes; /* their size, uncompressed */
    char from[96];            /* apply: the zip's file name the restore came from */
    char items[160];          /* the things it holds at the top, spaces between
                                ("memcard states config.toml") */
    int other_disc;           /* inspect: made while another disc than disc_now was played */
} IcoBackupManifest;

/* The conditions a backup and a restore need (backup_host.c samples them
   on the game's thread): nothing being written to the memory card
   (port/save/mc_host.h ico_mc_host_pending), no save state write in
   flight (state_file.h ico_state_file_busy), the disc idle
   (port/data/cdvd_host.h ico_cdvd_host_idle). */
typedef struct IcoBackupGates {
    int card_busy;
    int state_busy;
    int disc_busy;
} IcoBackupGates;

/* What a call did, for the caller's log line. */
typedef struct IcoBackupReport {
    int files;                /* files written, unpacked or moved into place */
    unsigned long long bytes; /* their size */
    int leftover;             /* apply: an unfinished unpacking was found and deleted */
    char why[300];            /* the failure in plain words, "" on success */
    /* apply, a failed one: the things a move back also failed for, so they
       are not as they were, each with where the player's own copy is
       ("memcard (yours is in memcard.old-1793530000)"), "" when every move
       was undone */
    char left[300];
    /* apply, at every start: the old copies an earlier restore left beside
       the user data (<name>.old-<time>: a run that ended between the two
       moves of one thing, or an undo that failed), "" none */
    char stale[300];
} IcoBackupReport;

/* Called after each file, done of total, on the caller's thread. */
typedef void (*IcoBackupProgressFn)(void *user, int done, int total);

/* 1 when name is one a backup may hold: relative, '/' between its parts,
   no empty part, no "." or "..", no '\\' or ':' or control character, at
   most 8 parts and 240 bytes, and under one of the backup's places: the
   manifest, config.toml, achievements.toml (each alone), a file below
   states/ or a memory card folder (memcard and memcard followed by '-',
   '_' or letters and digits). folder 1: a folder's name (a zip's
   directory entry, the trailing '/' taken off), which may be a place alone. */
int ico_backup_name_ok(const char *name, int folder);

/* Writes the backup of user_dir to zip_path: zip_path.part first, renamed
   over zip_path when complete (a failure leaves neither). Each file is
   stored (save states and pictures, already packed) or deflated at the
   fastest level; the manifest first. gates: a busy card or state write
   refuses (BUSY) before anything is read. NOTHING when there is nothing to
   back up, TOO_BIG past a bound, READ or WRITE with out->why. */
IcoBackupResult ico_backup_write(const char *user_dir, const char *zip_path,
                                 const IcoBackupInfo *info, const IcoBackupGates *gates,
                                 IcoBackupProgressFn fn, void *user, IcoBackupReport *out);

/* Reads zip_path's manifest and every entry's name and size (no data):
   OK with *m filled (other_disc against disc_now, which may be NULL), or
   NOT_BACKUP, NEWER, DAMAGED (the count or the sizes disagree with the
   manifest, an encrypted or unsupported entry), UNSAFE or TOO_BIG, with
   out->why (out may be NULL). */
IcoBackupResult ico_backup_inspect(const char *zip_path, const char *disc_now, IcoBackupManifest *m,
                                   IcoBackupReport *out);

/* The first step of a restore: gates (BUSY when anything is busy), then
   ico_backup_inspect, then every file unpacked under
   user_dir/ICO_BACKUP_STAGING_PART (an earlier one deleted first), each
   entry's data checked against its size and CRC, the manifest kept there
   with the zip's file name (from_name, shown in the next start's log), and
   the folder renamed to ICO_BACKUP_STAGING (an earlier one not yet put in
   place is replaced). Nothing else in user_dir changes; on a failure the
   unpacked files are deleted. *m (may be NULL) gets the manifest. */
IcoBackupResult ico_backup_restore(const char *zip_path, const char *from_name,
                                   const char *user_dir, const char *disc_now,
                                   const IcoBackupGates *gates, IcoBackupProgressFn fn, void *user,
                                   IcoBackupManifest *m, IcoBackupReport *out);

/* The second step, at the start: NONE when user_dir has no
   ICO_BACKUP_STAGING (an ICO_BACKUP_STAGING_PART left by a run that ended
   while unpacking is deleted, out->leftover 1). Else each thing it holds
   is moved into place as the header comment says, now (seconds) naming the
   old copies, and the staging folder deleted: OK with *m (from, items,
   files) and out->files the files put in place; WRITE (out->why names the
   move) with every move undone and the staging folder deleted, so the
   next start does not try again (out->left names any thing a move back
   failed for, its old copy kept); DAMAGED when its manifest cannot be
   read (the folder deleted).  Every call first lists the old copies
   already beside the user data (out->stale). */
IcoBackupResult ico_backup_apply_pending(const char *user_dir, long long now, IcoBackupManifest *m,
                                         IcoBackupReport *out);

/* "ico-pc-backup-YYYY-MM-DD.zip" for the local date of t, the name the
   save dialog suggests. */
void ico_backup_default_name(char *out, size_t size, long long t);

/* Tests: the bounds above lowered (0 for a bound: its default again). */
void ico_backup_test_bounds(int files, unsigned long long file_bytes, unsigned long long bytes);
/* Tests: the nth move ico_backup_apply_pending makes (1 the first, up to
   32) fails as a locked folder would; 0 none.  _also adds one more. */
void ico_backup_test_fail_move(int nth);
void ico_backup_test_fail_also(int nth);

#endif /* ICO_PLATFORM_BACKUP_H */
