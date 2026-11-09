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
 *   2. copies EXE to EXE.stamp-<pid>.tmp beside it and, in the copy:
 *      one RT_ICON a .ico image (ids 1..n), RT_GROUP_ICON 1 (the ICONDIR
 *      as a GRPICONDIR, port/platform/icon_stamp.c) and RT_RCDATA
 *      "ICO_ICON_STAMP" = STAMP (the icon's label and disc, which the game
 *      compares at its next start), all language neutral; the program's
 *      other resources (the manifest, the version information) are kept
 *   3. reads the copy's stamp and icon group back, checks that what runs
 *      is unchanged (the sections before the resources byte for byte, the
 *      entry point and the import, exception and TLS directories:
 *      ico_icon_stamp_pe_check), then puts the copy in EXE's place
 *      (ReplaceFileW: the file's attributes and security kept), so a
 *      failure at any step leaves EXE as it was
 *   4. tells the shell the icons changed (SHChangeNotify)
 *
 * Each run appends a line to LOG (logs\icon-stamp.log beside the game's
 * log, which is closed by then): the time, EXE, ICO and the result.
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
        log_line("refused: the icon file is not usable: %s (%lu bytes)", why, (unsigned long)icoN);
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

    static wchar_t tmp[32768];
    if (_snwprintf(tmp, sizeof(tmp) / sizeof(tmp[0]), L"%ls.stamp-%lu.tmp", s_exe,
                   (unsigned long)GetCurrentProcessId()) < 0) {
        log_line("not stamped: the path is too long");
        free(ico);
        return 1;
    }
    tmp[sizeof(tmp) / sizeof(tmp[0]) - 1] = L'\0';
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
    if (!verify(tmp, stamp, stampN, res.groupSize)) {
        log_line("not stamped: the copy's resources did not read back as written");
        DeleteFileW(tmp);
        free(ico);
        return 1;
    }
    if (!same_program(s_exe, tmp, err, sizeof(err))) {
        log_line("not stamped: the stamped copy is not the same program (%s)", err);
        DeleteFileW(tmp);
        free(ico);
        return 1;
    }
    if (!ReplaceFileW(s_exe, tmp, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL)) {
        char err2[600];
        error_text(GetLastError(), err, sizeof(err));
        if (!MoveFileExW(tmp, s_exe, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            error_text(GetLastError(), err2, sizeof(err2));
            log_line("not stamped: the stamped copy could not take the program's place (replace: "
                     "%s; move: %s)",
                     err, err2);
            DeleteFileW(tmp);
            free(ico);
            return 1;
        }
        log_line("note: the replace failed (%s); the stamped copy was moved over the program", err);
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
