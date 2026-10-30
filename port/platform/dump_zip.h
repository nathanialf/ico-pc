/*
 * port/platform/dump_zip.h
 *
 * F12's zip: the frame's .rddump, the tick before's (so rd_replay_tool
 * --interp rebuilds the pictures between the two) and a copy of the log in
 * one file, because GitHub issues refuse .rddump attachments but accept
 * .zip. The .png files are left out (far too big at high scales), and stay
 * loose files beside the zip.
 */
#ifndef ICO_PLATFORM_DUMP_ZIP_H
#define ICO_PLATFORM_DUMP_ZIP_H

#include <stddef.h>

/* Writes zip_path holding the file dump_path under its own name, then
   prev_path (NULL or "": none; the tick before, second so a zip's first
   .rddump stays the frame) and, when it can be read, the file log_path under
   its own name (log_path NULL or "" means no log). Both are taken as they are on disk now, so the dump must
   be closed. 0: both are in; 1: the dump alone (the log could not be read);
   -1: nothing written, why (when not NULL) holds the reason. After 0 or 1
   the loose dump file is deleted, the zip being where it lives now; after
   -1 it is kept. */
int ico_frame_dump_zip(const char *zip_path, const char *dump_path, const char *prev_path,
                       const char *log_path, char *why, size_t why_size);

/* Adds the file file_path to the zip zip_path (a zip this code wrote) under
   the name entry, keeping every entry it holds (the bug-report picture:
   the presented PNG exists only after the next present, so after the zip).
   0: added; -1: the zip is as it was, why (when not NULL) holding the
   reason.  *added_size (when not NULL) is the file's size in bytes. */
int ico_zip_add_file(const char *zip_path, const char *file_path, const char *entry, char *why,
                     size_t why_size, size_t *added_size);

/* The device-loss dump (window_host.c device_lost_quit): zip_path holding
   lost_path, then before_path (NULL or "": none), then log_path, each under
   its own name, as ico_frame_dump_zip writes its one dump (the same
   result, and the loose dumps deleted after a 0 or 1).  The lost frame is
   the first .rddump, so rd_replay_tool and the loss bisect
   (ico_dump_zip_extract) take it. */
#define ICO_LOSS_DUMP_FILES 2
int ico_loss_dump_zip(const char *zip_path, const char *lost_path, const char *before_path,
                      const char *log_path, char *why, size_t why_size);
/* dir/loss-<stamp>.zip into out; 0, or -1 when it does not fit */
int ico_loss_dump_path(char *out, size_t size, const char *dir, const char *stamp);
/* Deletes the oldest loss-*.zip files in dir until keep remain (a device
   lost at every start must not fill a phone); the number deleted, or -1
   when dir cannot be read. */
#define ICO_LOSS_DUMP_KEEP 3
int ico_loss_dump_prune(const char *dir, int keep);

/* The first entry of zip_path whose name ends in ".rddump", in a malloc
   block (free it) with its size in *size and, when name is not NULL, its
   name in name (name_size bytes). NULL when the zip cannot be read or holds
   no such entry. rd_replay_tool takes a F12 zip with this. */
void *ico_dump_zip_extract(const char *zip_path, size_t *size, char *name, size_t name_size);
/* The same for the entry after nth other .rddump entries (0: the first; 1:
   an F12 or loss zip's tick before) */
void *ico_dump_zip_extract_nth(const char *zip_path, int nth, size_t *size, char *name,
                               size_t name_size);

/* The aspect sweep (Shift+F12 in photo mode, window_host.c): one paused
   frame captured at ICO_SWEEP_COUNT aspects.  Aspect i has the name
   ico_sweep_aspect_name(i) ("4x3", "16x9", "32x9", "48x9", "60x9": the tokens
   tools/native_frame_check.py reads) and the value ico_sweep_aspect_value(i)
   (4/3, 16/9, 32/9, 48/9 and 60/9, the widest the frame holds). */
#define ICO_SWEEP_COUNT 5
const char *ico_sweep_aspect_name(int i);
float ico_sweep_aspect_value(int i);

/* Writes zip_path holding, for each aspect whose two files exist, the entries
   <name>.rddump and <name>.png from the files <prefix><name>.rddump and
   <prefix><name>.png (no log).  The 4:3 member comes first, so
   rd_replay_tool, which takes a zip's first .rddump, replays it.  Returns the
   number of aspects in the zip (0 to ICO_SWEEP_COUNT), the loose files of
   those deleted; -1 with nothing written (and the loose files kept) when no
   aspect has both files or the zip cannot be written, why (when not NULL)
   holding the reason. */
int ico_sweep_zip(const char *zip_path, const char *prefix, char *why, size_t why_size);

#endif /* ICO_PLATFORM_DUMP_ZIP_H */
