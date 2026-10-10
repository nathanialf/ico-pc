/*
 * port/platform/dump_zip.h
 *
 * F12's zip: the frame's .rddump and a copy of the log in one file, because
 * GitHub issues refuse .rddump attachments but accept .zip. The .png is left
 * out (far too big at high scales).
 */
#ifndef ICO_PLATFORM_DUMP_ZIP_H
#define ICO_PLATFORM_DUMP_ZIP_H

#include <stddef.h>

/* Writes zip_path holding the file dump_path under its own name and, when
   it can be read, the file log_path under its own name (log_path NULL or
   "" means no log). Both are taken as they are on disk now, so the dump must
   be closed. 0: both are in; 1: the dump alone (the log could not be read);
   -1: nothing written, why (when not NULL) holds the reason. */
int ico_frame_dump_zip(const char *zip_path, const char *dump_path, const char *log_path, char *why,
                       size_t why_size);

#endif /* ICO_PLATFORM_DUMP_ZIP_H */
