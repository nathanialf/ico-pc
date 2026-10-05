/*
 * port/platform/host_fs.h
 *
 * Host file calls on UTF-8 paths. Every path the port builds is UTF-8: the
 * per-user folder (SDL_GetPrefPath), the executable's folder
 * (host_config.c, from GetModuleFileNameW on Windows), the file dialog's
 * answer and the ini's values. On Windows the C library's fopen, _mkdir,
 * rename and stat, and the A Win32 calls, read a path in the process code
 * page, which is UTF-8 only under the executable's activeCodePage manifest
 * (port/platform/win/ico_pc.manifest, Windows 10 1903 and later). These
 * helpers convert to UTF-16 and call the wide functions, so a name outside
 * the ANSI code page works on every Windows the port runs on; a path that
 * is not valid UTF-8 falls back to the narrow call. Elsewhere they are the
 * plain POSIX calls. docs/port/DATA.md, "Paths".
 */
#ifndef ICO_PLATFORM_HOST_FS_H
#define ICO_PLATFORM_HOST_FS_H

#include <stdio.h>

/* fopen */
FILE *ico_fopen(const char *path, const char *mode);
/* mkdir (0777 on POSIX): 0, or -1 with errno (EEXIST when it exists) */
int ico_mkdir(const char *path);
/* rmdir: 0 or -1 */
int ico_rmdir(const char *path);
/* remove a file: 0 or -1 */
int ico_remove(const char *path);
/* rename from over to, replacing to when it exists (MoveFileExW with
   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH on Windows, rename(2)
   elsewhere, atomic on one volume): 0 or -1 */
int ico_rename_replace(const char *from, const char *to);
/* fflush, then the file's bytes onto the disk (_commit, i.e. FlushFileBuffers,
   on Windows; fsync elsewhere), for a file about to be moved over another
   with ico_rename_replace: without it a power cut can leave the new name on
   a file whose data never reached the disk. 0, or -1 with errno */
int ico_fsync(FILE *f);
/* 1 a directory, 0 another file, -1 nothing there; *size and *mtime (Unix
   seconds) when not NULL */
int ico_path_kind(const char *path, unsigned long long *size, long long *mtime);

#ifdef _WIN32

#include <wchar.h>

/* UTF-8 to a malloc'd UTF-16 string; NULL when s is not valid UTF-8 */
wchar_t *ico_widen(const char *s);
/* UTF-16 to UTF-8 into out: 0, or -1 when it does not fit */
int ico_narrow(const wchar_t *s, char *out, size_t size);

#endif
#endif /* ICO_PLATFORM_HOST_FS_H */
