/*
 * port/platform/android/iso_import.h
 *
 * The Android first start: the disc image the player chose in
 * the system's file picker (a content:// address, which SDL_IOFromFile opens
 * through the content resolver as a seekable stream) copied into the app's
 * files folder as Ico_PAL.iso, Ico_PAL.chd or Ico_PAL.bin (android_paths.h), where the
 * first-run extractor (port/data/extract.h) reads it like any file.
 * main_host.c deletes the copy once the game's data is mounted, unless
 * keep_image=1 is in ico-pc.ini.
 *
 * ico_iso_copy and the pure helpers build on every platform (the iso_import
 * ctest runs them on Linux with SDL_IOFromConstMem); ico_iso_import is
 * Android only.
 */
#ifndef ICO_PLATFORM_ANDROID_ISO_IMPORT_H
#define ICO_PLATFORM_ANDROID_ISO_IMPORT_H

#include <stddef.h>
#include <stdint.h>
#include <SDL3/SDL_iostream.h>
#include "extract.h" /* IcoExtractProgressFn */

/* What the copy reads and writes at a time. */
#define ICO_ISO_COPY_CHUNK (1u << 20)
/* The room kept free beside the copy: the extracted game data (about 1 GB)
   and a margin. SI gigabytes, as Android's Settings and Files app count. */
#define ICO_ISO_IMPORT_SPARE_BYTES 1200000000ull

/* ico_iso_import's results */
#define ICO_ISO_IMPORT_OK 0
#define ICO_ISO_IMPORT_CANCELLED 1
#define ICO_ISO_IMPORT_FAILED (-1)

/* Copies src from where it is to its end into dstTmp, which must end in
   ".tmp", ICO_ISO_COPY_CHUNK bytes at a time, flushed to the disk, then
   renames it to dstTmp without ".tmp" (replacing a file of that name).
   progress (may be NULL) is called with phase "copy" once before the first
   read and after each chunk with the bytes written so far and the stream's
   size (SDL_GetIOSize; when unknown, 0 until the last call, which reports
   done == total); a nonzero return cancels. Once the last chunk is written
   it is also called with phase "save" and 0 of 0, before the flush to the
   storage and the rename (the return is not used there). 0 when copied; 1 when cancelled;
   -1 with the reason in why (a read or write error, a stream shorter than
   its size). Cancelled or failed, neither dstTmp nor the final name is
   left behind. src stays open (the caller closes it). */
int ico_iso_copy(SDL_IOStream *src, const char *dstTmp, IcoExtractProgressFn progress, void *ctx,
                 char *why, size_t n);

/* The free space a copy of imageBytes needs: the image and
   ICO_ISO_IMPORT_SPARE_BYTES. */
uint64_t ico_iso_need_bytes(uint64_t imageBytes);

/* The message when the space is short: "This disc image needs N GB free in
   <folder>; M GB is free." and what to do, N rounded up and M down to a
   tenth of a GB. */
void ico_iso_space_text(char *out, size_t size, uint64_t needBytes, uint64_t freeBytes,
                        const char *folder);

/* "chd" when the first bytes of the file (head, headLen; may be NULL) are a
   CHD header ("MComprHD"), "bin" when 16 or more of them start with a raw
   CD image's 12-byte sync pattern; when the bytes are not known, by name
   (the picker's address or file name; may be NULL) ending in ".chd" or
   ".bin" in any case; else "iso". The extractor checks the header itself
   anyway. */
const char *ico_iso_ext_for(const char *name, const void *head, size_t headLen);

/* Whether the chosen file is a .cue sheet: its name ends in ".cue" in any
   case, or its first bytes are text that starts (after a UTF-8 byte order
   mark and blank space) with a cue command (FILE, REM, TITLE, CATALOG,
   PERFORMER, TRACK and the rest), in any case.  A sheet only lists the
   .bin, so the import refuses it. */
int ico_iso_is_cue(const char *name, const void *head, size_t headLen);

#ifdef __ANDROID__

/* The first start's import: opens uri (from SDL_ShowOpenFileDialog), checks
   that the files folder has ico_iso_need_bytes(size) free, and copies it to
   <files>/Ico_PAL.<ext> (ico_iso_copy, progress as there), whose path goes
   to out. ICO_ISO_IMPORT_OK; ICO_ISO_IMPORT_CANCELLED; or
   ICO_ISO_IMPORT_FAILED with why holding the text for the player's message
   box (the details are logged). */
int ico_iso_import(const char *uri, char *out, size_t outSize, IcoExtractProgressFn progress,
                   void *ctx, char *why, size_t n);

#endif

#endif /* ICO_PLATFORM_ANDROID_ISO_IMPORT_H */
