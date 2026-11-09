/*
 * port/platform/app_icon_exe.c
 *
 * The icon on the Windows program file (app_icon_exe.h): the exe's stamp at
 * start, and at the end of the run the helper tools\ico_icon_stamp.exe
 * started detached when the current disc's icon is newer than what the exe
 * carries.  The helper waits for the exe to be free, writes the icon
 * resources into a copy and puts the copy in its place (tools/
 * ico_icon_stamp.c); its result goes to logs\icon-stamp.log, which the next
 * start's log repeats (the game's own log is closed by the time it writes).
 *
 * The stamp is "<the icon's .txt> <disc id>": the .txt holds the package
 * label the icon was made by (app_icon.c), so a new release that makes the
 * icon again, or another disc, stamps the exe again.  The exe takes the
 * icon of the disc played last.
 */
#include "app_icon_exe.h"

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_icon.h"
#include "host_config.h"
#include "host_fs.h"
#include "icon_stamp.h"
#include "../core/region.h"

#define HELPER_REL "tools\\ico_icon_stamp.exe"
#define STAMP_LOG "icon-stamp.log"

static char s_exeStamp[256];  /* "" when the exe has none */
static char s_lastLine[1024]; /* the helper log's last line at start, "" */
static int s_ran;

/* the exe's RT_RCDATA "ICO_ICON_STAMP" into out: 0, or -1 when there is none */
static int exe_stamp(char *out, size_t size)
{
    HMODULE self = GetModuleHandleW(NULL);
    HRSRC r = FindResourceW(self, L"" ICO_ICON_STAMP_NAME, MAKEINTRESOURCEW(10) /* RT_RCDATA */);
    out[0] = '\0';
    if (r == NULL) {
        return -1;
    }
    HGLOBAL h = LoadResource(self, r);
    const void *p = h != NULL ? LockResource(h) : NULL;
    const DWORD n = SizeofResource(self, r);
    if (p == NULL || n == 0) {
        return -1;
    }
    return ico_icon_stamp_read(out, size, (const uint8_t *)p, n);
}

/* logs\icon-stamp.log's last line into out: 0, or -1 */
static int last_line(const char *path, char *out, size_t size)
{
    char buf[1024];
    FILE *f = ico_fopen(path, "rb");
    out[0] = '\0';
    if (f == NULL) {
        return -1;
    }
    long len = -1;
    if (fseek(f, 0, SEEK_END) == 0) {
        len = ftell(f);
    }
    const long from = len > (long)sizeof(buf) - 1 ? len - (long)sizeof(buf) + 1 : 0;
    size_t n = 0;
    if (len > 0 && fseek(f, from, SEEK_SET) == 0) {
        n = fread(buf, 1, sizeof(buf) - 1, f);
    }
    fclose(f);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        n--;
    }
    buf[n] = '\0';
    if (n == 0) {
        return -1;
    }
    const char *start = buf;
    for (size_t i = n; i > 0; i--) {
        if (buf[i - 1] == '\n') {
            start = buf + i;
            break;
        }
    }
    snprintf(out, size, "%s", start);
    return 0;
}

static int stamp_log_path(char *out, size_t size)
{
    char logs[1024];
    if (ico_host_logs_dir(logs, sizeof(logs)) != 0 || logs[0] == '\0') {
        out[0] = '\0';
        return -1;
    }
    return ico_path_join(out, size, logs, STAMP_LOG);
}

void ico_app_icon_exe_start(void)
{
    char path[1024], line[1024];
    if (exe_stamp(s_exeStamp, sizeof(s_exeStamp)) == 0 && s_exeStamp[0] != '\0') {
        fprintf(stderr, "window: icon: the exe carries the icon (stamp %s)\n", s_exeStamp);
    } else {
        s_exeStamp[0] = '\0';
        fprintf(stderr, "window: icon: the exe has no icon yet\n");
    }
    if (stamp_log_path(path, sizeof(path)) == 0 && last_line(path, line, sizeof(line)) == 0) {
        fprintf(stderr, "window: icon: the stamp helper's last line (logs\\" STAMP_LOG "): %s\n",
                line);
        snprintf(s_lastLine, sizeof(s_lastLine), "%s", line);
    }
}

/* the current disc's .ico and the stamp it would give: 0, or -1 (why says) */
static int wanted(char *ico, size_t icoSize, char *stamp, size_t stampSize, const char **why)
{
    char dir[1024], name[128], txt[1024], raw[128], label[128];
    const IcoRegionInfo *ri = ico_region_cur();
    if (ri == NULL || ri->disc_id == NULL || ico_host_pref_dir(dir, sizeof(dir)) != 0 ||
        dir[0] == '\0') {
        *why = "no user folder or disc";
        return -1;
    }
    snprintf(name, sizeof(name), "ico-pc-%s.ico", ri->disc_id);
    if (ico_app_icon_path_at(ico, icoSize, dir, name) != 0 || !ico_file_exists(ico)) {
        *why = "the title screen has not made this disc's icon yet";
        return -1;
    }
    snprintf(name, sizeof(name), "ico-pc-%s.txt", ri->disc_id);
    FILE *f = ico_app_icon_path_at(txt, sizeof(txt), dir, name) == 0 ? ico_fopen(txt, "rb") : NULL;
    size_t n = 0;
    if (f != NULL) {
        n = fread(raw, 1, sizeof(raw), f);
        fclose(f);
    }
    if (n == 0 || ico_icon_stamp_read(label, sizeof(label), (const uint8_t *)raw, n) != 0 ||
        label[0] == '\0' || ico_icon_stamp_text(stamp, stampSize, label, ri->disc_id) != 0) {
        *why = "the icon's stamp file is missing";
        return -1;
    }
    return 0;
}

/* CreateProcessW of the helper, detached and without a window, out of the
   game's job when the job lets it (a launcher that closes its job as the
   game ends would end the helper too).  0, or the error */
static DWORD spawn(const char *helper, const char *cmd)
{
    wchar_t *wHelper = ico_widen(helper);
    wchar_t *wCmd = ico_widen(cmd);
    DWORD err = 0;
    if (wHelper == NULL || wCmd == NULL) {
        err = ERROR_NO_UNICODE_TRANSLATION;
    } else {
        const DWORD base = DETACHED_PROCESS | CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
        STARTUPINFOW si;
        PROCESS_INFORMATION pi;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        memset(&pi, 0, sizeof(pi));
        /* no handles inherited; the current folder stays the game's */
        BOOL ok = CreateProcessW(wHelper, wCmd, NULL, NULL, FALSE, base | CREATE_BREAKAWAY_FROM_JOB,
                                 NULL, NULL, &si, &pi);
        if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {
            ok = CreateProcessW(wHelper, wCmd, NULL, NULL, FALSE, base, NULL, NULL, &si, &pi);
        }
        if (ok) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            err = GetLastError() != 0 ? GetLastError() : ERROR_GEN_FAILURE;
        }
    }
    free(wHelper);
    free(wCmd);
    return err;
}

void ico_app_icon_exe_at_exit(void)
{
    char ico[1024], stamp[256], exe[1024], dir[1024], helper[1024], logPath[1024];
    static char cmd[8192];
    const char *why = "";
    wchar_t w[1024];
    if (s_ran) {
        return;
    }
    s_ran = 1;
    if (wanted(ico, sizeof(ico), stamp, sizeof(stamp), &why) != 0) {
        fprintf(stderr, "window: icon: the exe's icon is not stamped: %s\n", why);
        return;
    }
    if (strcmp(stamp, s_exeStamp) == 0) {
        fprintf(stderr, "window: icon: the exe's icon is current (stamp %s)\n", stamp);
        return;
    }
    /* the helper refused this stamp for good last time (an unusable icon,
       a copy that was not the same program): no 64 MB copy at every exit;
       a new icon (a new stamp) is tried */
    if (ico_icon_stamp_refused(s_lastLine, stamp)) {
        fprintf(stderr,
                "window: icon: the exe's icon is not stamped: the helper refused stamp %s "
                "(logs\\" STAMP_LOG "); it is not tried again until the icon changes\n",
                stamp);
        return;
    }
    const DWORD n = GetModuleFileNameW(NULL, w, (DWORD)(sizeof(w) / sizeof(w[0])));
    if (n == 0 || n >= sizeof(w) / sizeof(w[0]) || ico_narrow(w, exe, sizeof(exe)) != 0 ||
        ico_host_exe_dir(dir, sizeof(dir)) != 0 ||
        ico_path_join(helper, sizeof(helper), dir, HELPER_REL) != 0) {
        fprintf(stderr, "window: icon: the exe's icon is not stamped: its path is not known\n");
        return;
    }
    if (!ico_file_exists(helper)) {
        fprintf(stderr,
                "window: icon: " HELPER_REL " is missing: the exe keeps its icon (stamp %s "
                "wanted)\n",
                stamp);
        return;
    }
    if (stamp_log_path(logPath, sizeof(logPath)) != 0 &&
        ico_path_join(logPath, sizeof(logPath), dir, "logs\\" STAMP_LOG) != 0) {
        snprintf(logPath, sizeof(logPath), "NUL");
    }
    size_t used = 0;
    if (ico_icon_stamp_quote(cmd, sizeof(cmd), &used, helper) != 0 ||
        ico_icon_stamp_quote(cmd, sizeof(cmd), &used, exe) != 0 ||
        ico_icon_stamp_quote(cmd, sizeof(cmd), &used, ico) != 0 ||
        ico_icon_stamp_quote(cmd, sizeof(cmd), &used, stamp) != 0 ||
        ico_icon_stamp_quote(cmd, sizeof(cmd), &used, logPath) != 0) {
        fprintf(stderr, "window: icon: the exe's icon is not stamped: the command line is too "
                        "long\n");
        return;
    }
    const DWORD err = spawn(helper, cmd);
    if (err != 0) {
        fprintf(stderr,
                "window: icon: " HELPER_REL " did not start (error %lu): the exe keeps "
                "its icon\n",
                (unsigned long)err);
        return;
    }
    fprintf(stderr,
            "window: icon: the exe's icon is stamped after this run (" HELPER_REL
            ", stamp %s; its result in logs\\" STAMP_LOG ")\n",
            stamp);
}
