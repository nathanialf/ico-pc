/*
 * tools/ico_icon_stamp.c
 *
 * ico_icon_stamp EXE ICO STAMP LOG
 *
 * Gives a Windows program file (EXE, the game's ico_pc_x64.exe) the icon in
 * ICO (the .ico the title screen's capture made, port/platform/app_icon.h)
 * as its own icon resource, the one Explorer, the taskbar's pinned button
 * and shortcuts show.  The game starts it as it exits (port/platform/
 * app_icon_exe.c): a running program cannot rewrite its own file.
 * docs/port/APP_ICON.md, "The exe's icon".
 *
 *   1. waits up to 10 s for EXE to open for writing (the game still
 *      exiting, a virus scanner reading the new file)
 *   2. removes what a run killed half-way left beside EXE (EXE.stamp-*
 *      files made before this run started), copies EXE to
 *      EXE.stamp-<pid>.tmp beside it and, in the copy:
 *      one RT_ICON a .ico image (ids 1..n), RT_GROUP_ICON 1 (the ICONDIR
 *      as a GRPICONDIR, port/platform/icon_stamp.c) and RT_RCDATA
 *      "ICO_ICON_STAMP" = STAMP (the icon's label and disc, which the game
 *      compares at its next start), all language neutral; the program's
 *      other resources (the manifest, the version information) are kept
 *   3. zeroes the copy's COFF symbol table pointer (EndUpdateResourceW
 *      drops the symbol table that followed the sections but keeps the
 *      header's pointer, which then points into the moved sections),
 *      reads the copy's stamp and icon group back, checks that what runs
 *      is unchanged (the sections before the resources byte for byte, the
 *      entry point and the import, exception and TLS directories:
 *      ico_icon_stamp_pe_check), then puts the copy in EXE's place
 *      (ReplaceFileW with the old EXE kept as EXE.stamp-old-<pid> until
 *      the replace is done: the file's attributes and security kept), so a
 *      failure at any step leaves EXE as it was; when a failed replace
 *      leaves no EXE (Microsoft's ReplaceFileW page: errors 1176 and 1177
 *      can leave the old file renamed), the backup, or else the stamped
 *      copy, is put back in EXE's place, and a copy is never deleted while
 *      EXE is missing
 *   4. tells the shell the icons changed (SHChangeNotify)
 *
 * Each run appends a line to LOG (logs\icon-stamp.log beside the game's
 * log, which is closed by then): the time, EXE, ICO and the result, and a
 * line for each recovery step.  A refusal that the same icon and program
 * would meet again (an unusable icon file, a copy that does not read back
 * or is not the same program) is written "refused stamp \"STAMP\": ..."
 * (ico_icon_stamp_refusal): the game reads the log's last line at start
 * and does not start the helper again for that stamp.
 * Exit 0 stamped, 1 EXE could not be written (a folder the player cannot
 * write to, still in use after 10 s: nothing changed), 2 a bad ICO or bad
 * arguments.
 *
 * A Windows program with no window (GUI subsystem, nothing shown); paths in
 * UTF-16 from the command line.
 */
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "icon_stamp.h"

#define WAIT_MS 10000
#define WAIT_STEP_MS 100
#define ICO_MAX_BYTES (16u << 20)

static const wchar_t *s_logPath;

static const wchar_t *s_exe = L"?";

static const wchar_t *s_ico = L"?";

/* UTF-16 to UTF-8 into out ("?" when it does not convert) */
static void narrow(const wchar_t *w, char *out, int size)
{
    if (w == NULL || WideCharToMultiByte(CP_UTF8, 0, w, -1, out, size, NULL, NULL) == 0) {
        snprintf(out, (size_t)size, "?");
    }
}

/* the system's words for an error code, one line */
static void error_text(DWORD code, char *out, size_t size)
{
    wchar_t w[256];
    char m[512];
    DWORD n = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, code,
                             0, w, (DWORD)(sizeof(w) / sizeof(w[0])), NULL);
    while (n > 0 &&
           (w[n - 1] == L'\r' || w[n - 1] == L'\n' || w[n - 1] == L' ' || w[n - 1] == L'.')) {
        w[--n] = L'\0';
    }
    if (n == 0) {
        snprintf(out, size, "error %lu", (unsigned long)code);
        return;
    }
    narrow(w, m, (int)sizeof(m));
    snprintf(out, size, "error %lu: %s", (unsigned long)code, m);
}

/* one line onto the log: the local time, the exe, the .ico, the result */
static void log_line(const char *fmt, ...)
{
    char text[1024], exe[1024], ico[1024];
    SYSTEMTIME t;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    narrow(s_exe, exe, (int)sizeof(exe));
    narrow(s_ico, ico, (int)sizeof(ico));
    GetLocalTime(&t);
    if (s_logPath == NULL) {
        fprintf(stderr, "ico_icon_stamp: %s\n", text);
        return;
    }
    FILE *f = _wfopen(s_logPath, L"ab");
    if (f == NULL) {
        return;
    }
    fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u ico_icon_stamp: %s | exe %s | icon %s\r\n",
            (unsigned)t.wYear, (unsigned)t.wMonth, (unsigned)t.wDay, (unsigned)t.wHour,
            (unsigned)t.wMinute, (unsigned)t.wSecond, text, exe, ico);
    fclose(f);
}

static uint8_t *read_file(const wchar_t *path, size_t *n, DWORD *err)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;
    uint8_t *buf = NULL;
    *n = 0;
    *err = 0;
    if (h == INVALID_HANDLE_VALUE) {
        *err = GetLastError();
        return NULL;
    }
    if (!GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > ICO_MAX_BYTES) {
        *err = ERROR_INVALID_DATA;
    } else {
        DWORD got = 0;
        buf = malloc((size_t)size.QuadPart);
        if (buf == NULL) {
            *err = ERROR_NOT_ENOUGH_MEMORY;
        } else if (!ReadFile(h, buf, (DWORD)size.QuadPart, &got, NULL) ||
                   got != (DWORD)size.QuadPart) {
            *err = GetLastError() != 0 ? GetLastError() : ERROR_READ_FAULT;
            free(buf);
            buf = NULL;
        } else {
            *n = (size_t)size.QuadPart;
        }
    }
    CloseHandle(h);
    return buf;
}

/* 0 once exe opens for writing; else the last error (a sharing violation
   for the whole wait, or at once anything else: access denied in a folder
   the player cannot write to, a missing file) */
static DWORD wait_writable(const wchar_t *exe, DWORD *waitedMs)
{
    DWORD err = 0;
    for (DWORD ms = 0;; ms += WAIT_STEP_MS) {
        HANDLE h = CreateFileW(exe, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, NULL);
        *waitedMs = ms;
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            return 0;
        }
        err = GetLastError();
        if ((err != ERROR_SHARING_VIOLATION && err != ERROR_LOCK_VIOLATION) || ms >= WAIT_MS) {
            return err;
        }
        Sleep(WAIT_STEP_MS);
    }
}

static int exists(const wchar_t *path)
{
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

/* a refusal for good: the line the game reads (icon_stamp.h
   ico_icon_stamp_refused), then the reason */
static void log_refused(const char *stamp, const char *fmt, ...)
{
    char head[300], text[900];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (ico_icon_stamp_refusal(head, sizeof(head), stamp) != 0) {
        snprintf(head, sizeof(head), "refused:");
    }
    log_line("%s %s; the game does not try this icon again", head, text);
}

/* EXE.stamp-* files beside exe made before this run started (a helper
   killed between its copy and the replace: a 64 MB copy, or the old
   program kept as the backup after EXE was put back) removed.  Runs once
   exe is free, so EXE exists. */
static void remove_leftovers(const wchar_t *exe)
{
    static wchar_t pattern[32768], path[32768];
    FILETIME created, exited, kernel, user;
    WIN32_FIND_DATAW fd;
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) ||
        _snwprintf(pattern, sizeof(pattern) / sizeof(pattern[0]), L"%ls.stamp-*", exe) < 0) {
        return;
    }
    pattern[sizeof(pattern) / sizeof(pattern[0]) - 1] = L'\0';
    /* the folder part of exe, for the found names */
    const wchar_t *slash = wcsrchr(exe, L'\\');
    const wchar_t *fwd = wcsrchr(exe, L'/');
    if (fwd != NULL && (slash == NULL || fwd > slash)) {
        slash = fwd;
    }
    const size_t dirLen = slash != NULL ? (size_t)(slash - exe) + 1 : 0;
    HANDLE f = FindFirstFileW(pattern, &fd);
    if (f == INVALID_HANDLE_VALUE) {
        return;
    }
    int removed = 0, kept = 0;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ||
            CompareFileTime(&fd.ftCreationTime, &created) >= 0) {
            continue; /* this run's, or another helper's that started after it */
        }
        if (_snwprintf(path, sizeof(path) / sizeof(path[0]), L"%.*ls%ls", (int)dirLen, exe,
                       fd.cFileName) < 0) {
            continue;
        }
        path[sizeof(path) / sizeof(path[0]) - 1] = L'\0';
        if (DeleteFileW(path)) {
            removed++;
        } else {
            kept++;
        }
    } while (FindNextFileW(f, &fd));
    FindClose(f);
    if (removed > 0 || kept > 0) {
        log_line("note: %d file(s) an earlier run left beside the program removed, %d could not be",
                 removed, kept);
    }
}

/* the copy's COFF symbol table pointer and count zeroed (icon_stamp.h
   ico_icon_stamp_clear_symbols): 0, or the error */
static DWORD clear_symbols(const wchar_t *path)
{
    uint8_t head[4096];
    DWORD got = 0, put = 0, err = 0;
    HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return GetLastError();
    }
    if (!ReadFile(h, head, sizeof(head), &got, NULL)) {
        err = GetLastError();
    } else if (ico_icon_stamp_clear_symbols(head, got) != 0) {
        err = ERROR_BAD_EXE_FORMAT;
    } else if (SetFilePointer(h, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
               !WriteFile(h, head, got, &put, NULL) || put != got) {
        err = GetLastError() != 0 ? GetLastError() : ERROR_WRITE_FAULT;
    }
    CloseHandle(h);
    return err;
}

/* the copy's own resources read back: the stamp's bytes and the group's
   size.  1 when both are what was written */
static int verify(const wchar_t *path, const char *stamp, size_t stampN, size_t groupSize)
{
    HMODULE m =
        LoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    int ok = 0;
    if (m == NULL) {
        return 0;
    }
    HRSRC r = FindResourceW(m, L"" ICO_ICON_STAMP_NAME, MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    HRSRC g = FindResourceW(m, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */);
    if (r != NULL && g != NULL && SizeofResource(m, r) == stampN &&
        SizeofResource(m, g) == groupSize) {
        HGLOBAL h = LoadResource(m, r);
        const void *p = h != NULL ? LockResource(h) : NULL;
        ok = p != NULL && memcmp(p, stamp, stampN) == 0;
    }
    FreeLibrary(m);
    return ok;
}

/* a read-only view of the whole file: the pointer (NULL on a failure, err
   set), its size in *n; unmap with UnmapViewOfFile */
static const uint8_t *map_file(const wchar_t *path, size_t *n, DWORD *err)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    LARGE_INTEGER size;
    const uint8_t *view = NULL;
    *n = 0;
    *err = 0;
    if (f == INVALID_HANDLE_VALUE) {
        *err = GetLastError();
        return NULL;
    }
    if (!GetFileSizeEx(f, &size) || size.QuadPart <= 0 || (uint64_t)size.QuadPart > SIZE_MAX) {
        *err = ERROR_INVALID_DATA;
    } else {
        HANDLE m = CreateFileMappingW(f, NULL, PAGE_READONLY, 0, 0, NULL);
        if (m == NULL) {
            *err = GetLastError();
        } else {
            view = MapViewOfFile(m, FILE_MAP_READ, 0, 0, 0);
            if (view == NULL) {
                *err = GetLastError();
            } else {
                *n = (size_t)size.QuadPart;
            }
            CloseHandle(m); /* the view keeps the mapping */
        }
    }
    CloseHandle(f);
    return view;
}

/* the stamped copy against the program (icon_stamp.h ico_icon_stamp_pe_check):
   1 when what runs is unchanged; else 0 with the reason in why */
static int same_program(const wchar_t *exe, const wchar_t *copy, char *why, size_t size)
{
    size_t an = 0, bn = 0;
    DWORD e1 = 0, e2 = 0;
    const uint8_t *a = map_file(exe, &an, &e1);
    const uint8_t *b = map_file(copy, &bn, &e2);
    const char *text = NULL;
    int ok = 0;
    if (a == NULL || b == NULL) {
        error_text(a == NULL ? e1 : e2, why, size);
    } else if (ico_icon_stamp_pe_check(a, an, b, bn, &text) != 0) {
        snprintf(why, size, "%s", text != NULL ? text : "?");
    } else {
        ok = 1;
    }
    if (a != NULL) {
        UnmapViewOfFile(a);
    }
    if (b != NULL) {
        UnmapViewOfFile(b);
    }
    return ok;
}

/* the resources into the copy at tmp: 0, or the error */
static DWORD update(const wchar_t *tmp, const IcoIconStampResources *res, const char *stamp,
                    size_t stampN)
{
    const WORD lang = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    HANDLE h = BeginUpdateResourceW(tmp, FALSE);
    if (h == NULL) {
        return GetLastError();
    }
    BOOL ok = TRUE;
    for (int i = 0; ok && i < res->count; i++) {
        ok = UpdateResourceW(h, MAKEINTRESOURCEW(3) /* RT_ICON */,
                             MAKEINTRESOURCEW(res->image[i].id), lang, (void *)res->image[i].data,
                             res->image[i].size);
    }
    ok = ok && UpdateResourceW(h, MAKEINTRESOURCEW(14) /* RT_GROUP_ICON */, MAKEINTRESOURCEW(1),
                               lang, (void *)res->group, (DWORD)res->groupSize);
    ok = ok && UpdateResourceW(h, MAKEINTRESOURCEW(10) /* RT_RCDATA */, L"" ICO_ICON_STAMP_NAME,
                               lang, (void *)stamp, (DWORD)stampN);
    if (!ok) {
        const DWORD err = GetLastError();
        EndUpdateResourceW(h, TRUE); /* discard */
        return err != 0 ? err : ERROR_INVALID_DATA;
    }
    if (!EndUpdateResourceW(h, FALSE)) {
        const DWORD err = GetLastError();
        return err != 0 ? err : ERROR_WRITE_FAULT;
    }
    return 0;
}

static int usage(void)
{
    log_line("usage: ico_icon_stamp EXE ICO STAMP LOG (it is started by the game as it exits)");
    return 2;
}

int main(void)
{
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    char err[600], stamp[256];
    if (argv == NULL) {
        return usage();
    }
    if (argc >= 5) {
        s_logPath = argv[4];
    }
    if (argc != 5 || argv[1][0] == L'\0' || argv[2][0] == L'\0') {
        return usage();
    }
    s_exe = argv[1];
    s_ico = argv[2];
    if (WideCharToMultiByte(CP_UTF8, 0, argv[3], -1, stamp, (int)sizeof(stamp), NULL, NULL) == 0 ||
        stamp[0] == '\0') {
        log_line("refused: the stamp text is empty or too long");
        return 2;
    }
    const size_t stampN = strlen(stamp);

    size_t icoN = 0;
    DWORD e = 0;
    uint8_t *ico = read_file(s_ico, &icoN, &e);
    if (ico == NULL) {
        error_text(e, err, sizeof(err));
        log_line("refused: the icon file could not be read (%s)", err);
        return 2;
    }
    static IcoIconStampResources res;
    const char *why = "";
    if (ico_icon_stamp_build(ico, icoN, &res, &why) != 0) {
        log_refused(stamp, "the icon file is not usable: %s (%lu bytes)", why, (unsigned long)icoN);
        free(ico);
        return 2;
    }

    DWORD waited = 0;
    e = wait_writable(s_exe, &waited);
    if (e != 0) {
        error_text(e, err, sizeof(err));
        if (e == ERROR_SHARING_VIOLATION || e == ERROR_LOCK_VIOLATION) {
            log_line("not stamped: the program file was still in use after %lu ms (%s); the next "
                     "exit tries again",
                     (unsigned long)waited, err);
        } else {
            log_line("not stamped: the program file cannot be written (%s): the folder may be one "
                     "this account cannot write to; the program keeps its icon",
                     err);
        }
        free(ico);
        return 1;
    }

    remove_leftovers(s_exe);
    static wchar_t tmp[32768], old[32768];
    if (_snwprintf(tmp, sizeof(tmp) / sizeof(tmp[0]), L"%ls.stamp-%lu.tmp", s_exe,
                   (unsigned long)GetCurrentProcessId()) < 0 ||
        _snwprintf(old, sizeof(old) / sizeof(old[0]), L"%ls.stamp-old-%lu", s_exe,
                   (unsigned long)GetCurrentProcessId()) < 0) {
        log_line("not stamped: the path is too long");
        free(ico);
        return 1;
    }
    tmp[sizeof(tmp) / sizeof(tmp[0]) - 1] = L'\0';
    old[sizeof(old) / sizeof(old[0]) - 1] = L'\0';
    if (!CopyFileW(s_exe, tmp, FALSE)) {
        error_text(GetLastError(), err, sizeof(err));
        log_line("not stamped: the copy beside the program could not be made (%s)", err);
        free(ico);
        return 1;
    }
    e = update(tmp, &res, stamp, stampN);
    if (e != 0) {
        error_text(e, err, sizeof(err));
        log_line("not stamped: the icon resources could not be written into the copy (%s)", err);
        DeleteFileW(tmp);
        free(ico);
        return 1;
    }
    e = clear_symbols(tmp);
    if (e != 0) {
        error_text(e, err, sizeof(err));
        log_line("not stamped: the copy's symbol table pointer could not be cleared (%s)", err);
        DeleteFileW(tmp);
        free(ico);
        return 1;
    }
    if (!verify(tmp, stamp, stampN, res.groupSize)) {
        log_refused(stamp, "the copy's resources did not read back as written");
        DeleteFileW(tmp);
        free(ico);
        return 1;
    }
    if (!same_program(s_exe, tmp, err, sizeof(err))) {
        log_refused(stamp, "the stamped copy is not the same program (%s)", err);
        DeleteFileW(tmp);
        free(ico);
        return 1;
    }
    if (!ReplaceFileW(s_exe, tmp, old, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
        const DWORD re = GetLastError();
        char err2[600];
        error_text(re, err, sizeof(err));
        if (!exists(s_exe)) {
            /* 1177 (the old program kept under the backup's name) or a
               failure part-way: the program file is put back first */
            if (exists(old) && MoveFileExW(old, s_exe, MOVEFILE_WRITE_THROUGH)) {
                log_line("not stamped: the replace failed (%s) and left no program file; the "
                         "program was put back from its backup; the next exit tries again",
                         err);
                DeleteFileW(tmp);
                free(ico);
                return 1;
            }
            error_text(GetLastError(), err2, sizeof(err2));
            if (MoveFileExW(tmp, s_exe, MOVEFILE_WRITE_THROUGH)) {
                log_line("note: the replace failed (%s) and left no program file, the backup "
                         "could not be put back (%s): the stamped copy was moved into its place",
                         err, err2);
                DeleteFileW(old);
            } else {
                char err3[600], oldU[1024], tmpU[1024];
                error_text(GetLastError(), err3, sizeof(err3));
                narrow(old, oldU, (int)sizeof(oldU));
                narrow(tmp, tmpU, (int)sizeof(tmpU));
                log_line("FAILED: the replace failed (%s) and left no program file; neither the "
                         "backup (%s) nor the stamped copy (%s) could be moved back: the "
                         "program is %s (the stamped one %s): rename either to the program's "
                         "name",
                         err, err2, err3, oldU, tmpU);
                free(ico);
                return 1;
            }
        } else if (!MoveFileExW(tmp, s_exe, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error_text(GetLastError(), err2, sizeof(err2));
            if (!exists(s_exe)) {
                char tmpU[1024];
                narrow(tmp, tmpU, (int)sizeof(tmpU));
                log_line("FAILED: the replace (%s) and the move (%s) failed and the program "
                         "file is gone; the stamped copy is kept as %s: rename it to the "
                         "program's name",
                         err, err2, tmpU);
                free(ico);
                return 1;
            }
            log_line("not stamped: the stamped copy could not take the program's place (replace: "
                     "%s; move: %s); the program is unchanged, the next exit tries again",
                     err, err2);
            DeleteFileW(tmp);
            DeleteFileW(old);
            free(ico);
            return 1;
        } else {
            log_line("note: the replace failed (%s); the stamped copy was moved over the program",
                     err);
        }
    }
    if (exists(old) && !DeleteFileW(old)) {
        error_text(GetLastError(), err, sizeof(err));
        log_line("note: the old program's backup could not be removed (%s); the next run removes "
                 "it",
                 err);
    }
    int pngs = 0;
    for (int i = 0; i < res.count; i++) {
        pngs += res.image[i].png;
    }
    SHChangeNotify(SHCNE_UPDATEITEM, SHCNF_PATHW, s_exe, NULL);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    log_line("stamped: %d images (%d PNG), stamp \"%s\", after %lu ms", res.count, pngs, stamp,
             (unsigned long)waited);
    free(ico);
    LocalFree(argv);
    return 0;
}
