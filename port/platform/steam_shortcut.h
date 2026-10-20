/*
 * port/platform/steam_shortcut.h
 *
 * Settings > Extras > Add to Steam: a non-Steam shortcut for this program in
 * the most recent Steam user's library (userdata/<id>/config/shortcuts.vdf,
 * a binary VDF file). Windows, Linux and Steam Deck only: the Android and
 * iOS builds do not compile it, and the macOS build does not offer the row
 * (no /proc for the program's own path and no way here to tell that Steam
 * runs; window_host.c).
 *
 * The file is edited by inserting one entry before the end of its
 * "shortcuts" map, so every existing entry keeps its bytes; the file as it
 * was is copied to shortcuts.vdf.bak first. Steam rewrites this file from
 * memory when it exits, so nothing is written while Steam runs. No artwork
 * is added.
 */
#ifndef ICO_PLATFORM_STEAM_SHORTCUT_H
#define ICO_PLATFORM_STEAM_SHORTCUT_H

#include <stddef.h>
#include <stdint.h>

#define ICO_STEAM_ADDED 0     /* the shortcut was written */
#define ICO_STEAM_EXISTS 1    /* an entry with this program's Exe is there already */
#define ICO_STEAM_RUNNING 2   /* Steam is running: nothing was touched */
#define ICO_STEAM_NOT_FOUND 3 /* no Steam folder with a userdata folder */
#define ICO_STEAM_NO_USER 4   /* no Steam user folder to put the shortcut in */
#define ICO_STEAM_FAILED 5    /* the file could not be read, understood or written */

#define ICO_STEAM_PATH_MAX 512

/* Where things are and how to tell that Steam runs. ico_steam_env_default
   fills it for this machine; the tests fill it with their own. An empty
   string ends a list early. */
typedef struct IcoSteamEnv {
    char roots[6][ICO_STEAM_PATH_MAX];    /* candidate Steam folders, first match wins */
    char pidFiles[3][ICO_STEAM_PATH_MAX]; /* files holding Steam's pid (Linux) */
    int (*pidAlive)(long pid);            /* NULL: the platform's check */
    int (*activeProcessPid)(long *pid);   /* the registry's ActiveProcess pid (Windows):
                                             1 and *pid when set, else 0; NULL: none */
    int (*processRunning)(void);          /* a process named steam.exe (Windows); NULL: none */
    int iconIsExe;                        /* the entry's icon is the program (Windows) */
    int caselessPaths;                    /* the duplicate check ignores case (Windows) */
} IcoSteamEnv;

/* The entry to add. exe and startDir are the plain paths; the entry stores
   them in quotes. */
typedef struct IcoSteamEntry {
    const char *name;
    const char *exe;
    const char *startDir;
    const char *icon; /* "" for none */
} IcoSteamEntry;

/* CRC-32 (zlib's), and Steam's id for a non-Steam shortcut: the CRC of the
   quoted Exe followed by the AppName, with the top bit set, as a signed
   32-bit value (how the file stores it). */
uint32_t ico_steam_crc32(const void *data, size_t n);
int32_t ico_steam_appid(const char *quotedExe, const char *appName);

/* The shortcuts file's bytes (n may be 0 for no file). Find: 1 an entry's
   Exe equals exe (the quoted form or the plain one), 0 none, -1 not a
   shortcuts file. Append: the file with the entry added as the next index,
   in *out (malloc; free it) and *outN; 0, or -1 for a file that is not a
   shortcuts file. */
int ico_steam_vdf_find_exe(const uint8_t *vdf, size_t n, const char *exe, int caseless);
int ico_steam_vdf_append(const uint8_t *vdf, size_t n, const IcoSteamEntry *e, uint8_t **out,
                         size_t *outN);

/* The user ids (low 32 bits of the Steam id) in loginusers.vdf's text, the
   most recent user first. Fills ids[0..max); returns the count. */
int ico_steam_login_users(const char *text, size_t n, uint32_t *ids, int max);

/* 1 when Steam runs by the env's checks. */
int ico_steam_running(const IcoSteamEnv *env);
/* 1 when pid is a live Steam process: alive and named steam (a stale pid
   file whose pid another program now has does not count). */
int ico_steam_pid_alive(long pid);

/* The whole job against env: ICO_STEAM_*. */
int ico_steam_add_shortcut(const IcoSteamEnv *env, const IcoSteamEntry *e);

/* This machine's env, and this program's entry (name "ICO"), added with it.
   Returns ICO_STEAM_*. */
void ico_steam_env_default(IcoSteamEnv *env);
int ico_steam_add_this_game(void);
/* The path the shortcut starts, UTF-8 with the platform's separators: this
   program's own (GetModuleFileNameW, /proc/self/exe), or on Linux the
   AppImage file it runs from (ico_appimage_file, host_fs.h); the entry's
   start folder is its folder. 0, or -1. */
int ico_steam_self_exe(char *out, size_t size);

#endif
