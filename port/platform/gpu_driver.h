/*
 * port/platform/gpu_driver.h
 *
 * Graphics driver packages for the Android build: the
 * community driver packages for Adreno GPUs (Turnip, the patched Qualcomm
 * drivers) that libadrenotools loads, as zip files the player adds from the
 * Settings page.  A package is flat: meta.json, the
 * driver library it names (libraryName) and the libraries that one needs.
 *
 *   meta.json  {"schemaVersion": 1, "name": "Turnip", "description": "...",
 *               "author": "...", "packageVersion": "1", "vendor": "Mesa",
 *               "driverVersion": "24.1", "minApi": 28,
 *               "libraryName": "vulkan.ad07XX.so"}
 *
 * Installed packages live in folders under one root (on the phone the
 * app's internal files folder, drivers/, the only place the system lets
 * the app load a library from):
 *
 *   <root>/<folder>/meta.json, <libraryName>, ...   one installed package
 *   <root>/<folder>.tmp/                            an install in progress
 *   <root>/starting                                 the trial marker
 *
 * <folder> is the package's name and driverVersion with every character
 * outside [A-Za-z0-9._-] made '_' (ico_gpu_driver_folder_name).  The trial
 * marker holds the folder of the driver a start is trying: written before
 * the driver is opened, deleted once it has drawn for a while, so a marker
 * still there at the next start means that start died inside the driver.
 *
 * Pure C over host_fs.h, miniz and port/data's json.h, no Android call, so
 * it is built and unit-tested on every platform (test/gpu_driver_test.c);
 * every call takes the root folder.  The loading itself is
 * android/gpu_driver_android.c.
 */
#ifndef ICO_PLATFORM_GPU_DRIVER_H
#define ICO_PLATFORM_GPU_DRIVER_H

#include <stddef.h>
#include <stdint.h>

/* the largest file a package may hold, unpacked */
#define ICO_GPU_DRIVER_ENTRY_MAX (64ull << 20)
/* the most files a package may hold */
#define ICO_GPU_DRIVER_FILES_MAX 64
/* the largest meta.json read */
#define ICO_GPU_DRIVER_META_MAX (64u << 10)
/* the most installed drivers listed */
#define ICO_GPU_DRIVER_LIST_MAX 32

/* results */
#define ICO_GPU_DRIVER_OK 0
#define ICO_GPU_DRIVER_BAD (-1)     /* not a usable package (why says what) */
#define ICO_GPU_DRIVER_NOSPACE (-2) /* the disk is full */
#define ICO_GPU_DRIVER_IO (-3)      /* another read or write error */

typedef struct IcoGpuDriverMeta {
    int schemaVersion; /* 0 when absent */
    int minApi;        /* the lowest Android API level; 0 when absent */
    char name[128];    /* required, not empty; a longer name is cut */
    char description[512];
    char author[128];
    char packageVersion[64];
    char vendor[64];
    char driverVersion[64]; /* may be empty; longer is rejected */
    char libraryName[128];  /* required: ^[A-Za-z0-9._-]+\.so$ */
} IcoGpuDriverMeta;

typedef struct IcoGpuDriver {
    char folder[160]; /* the folder under the root */
    IcoGpuDriverMeta meta;
} IcoGpuDriver;

/* Reads meta.json's n bytes into *out (zeroed first).  ICO_GPU_DRIVER_OK,
   or ICO_GPU_DRIVER_BAD with the reason in why (may be NULL): not JSON, not
   an object, no name, no or a bad libraryName, a minApi or schemaVersion
   that is not a whole number, a string field that is not a string. */
int ico_gpu_driver_parse_meta(const char *text, size_t n, IcoGpuDriverMeta *out, char *why,
                              size_t whyN);

/* 1 when name matches ^[A-Za-z0-9._-]+\.so$ (and has no ".."), else 0. */
int ico_gpu_driver_library_name_ok(const char *name);

/* 1 when name may be a file of a package: not empty, no '/', '\\', ':',
   "..", control character, DEL or leading '.', at most 127 bytes. */
int ico_gpu_driver_entry_name_ok(const char *name);

/* The folder a package installs into: name, then '-' and driverVersion
   when it has one, each character outside [A-Za-z0-9._-] made '_' (a run
   of them one '_'), each part at most 63 bytes; never empty, never
   starting with '.', never a name the root's own files use. */
void ico_gpu_driver_folder_name(const IcoGpuDriverMeta *m, char *out, size_t n);

/* 1 when folder is a name ico_gpu_driver_folder_name could give (so it is
   safe to join under the root), else 0. */
int ico_gpu_driver_folder_ok(const char *folder);

/* What the Settings page shows: the name, and the driver version after it
   when the name does not already hold it. */
void ico_gpu_driver_display_name(const IcoGpuDriverMeta *m, char *out, size_t n);

/* Installs the package zip at zipPath under root (made if missing): every
   file checked first (flat names only, ico_gpu_driver_entry_name_ok; at
   most ICO_GPU_DRIVER_FILES_MAX files of at most entryMax bytes each;
   meta.json valid; its libraryName in the zip; minApi <= apiLevel), then
   unpacked into <root>/<folder>.tmp/ and moved to <root>/<folder>/,
   replacing a package installed there before.  The folder goes to folder
   (may be NULL).  ICO_GPU_DRIVER_OK; _BAD, _NOSPACE or _IO with the reason
   in why; on failure nothing new is left under root. */
int ico_gpu_driver_install_zip(const char *root, const char *zipPath, int apiLevel, char *folder,
                               size_t folderN, char *why, size_t whyN);
/* The same with another size limit per file (the tests). */
int ico_gpu_driver_install_zip_max(const char *root, const char *zipPath, int apiLevel,
                                   uint64_t entryMax, char *folder, size_t folderN, char *why,
                                   size_t whyN);

/* The installed packages under root, in byte order of their folders, at
   most max: each folder (not ending in ".tmp") whose meta.json parses and
   whose libraryName is there.  The count (0 when root does not exist). */
int ico_gpu_driver_list(const char *root, IcoGpuDriver *out, int max);

/* The driver zips lying in the folder dir (the game's drivers folder in the
   app's files, where a USB cable can put them): the names of its files that
   end in ".zip" in any case, hidden files and the installer's own
   "incoming.*" left out, in the order of ico_dir_walk (byte order), at most
   max, each name under 160 bytes.  The count (0 when dir does not exist). */
int ico_gpu_driver_list_zips(const char *dir, char (*names)[160], int max);

/* Deletes <root>/<folder>/ and its files.  0, or -1 (a bad folder name, or
   something could not be deleted). */
int ico_gpu_driver_remove(const char *root, const char *folder);

/* Install-time check of a library's ELF program headers against the
   phone's memory page size (Android 15 on a phone with 16 KB pages loads a
   library only when its PT_LOAD segments are aligned to the page: a driver
   built with 4 KB alignment does not load there).  bytes is the start of
   the file (its first n bytes, 64 KB is plenty: the program header table
   sits right after the ELF header).  0 only when the file is a
   little-endian ELF whose program header table lies in the n bytes and has a
   PT_LOAD with p_align under pageSize, and pageSize is 16384 or more; 1 in
   every other case (a 4 KB page size, a file that is not ELF, a table past
   the n bytes: nothing is refused on what cannot be read).  *minAlign (may
   be NULL) gets the smallest PT_LOAD p_align read, 0 when none was. */
#define ICO_GPU_DRIVER_PAGES_MESSAGE                                                               \
    "this driver package is not built for this phone's 16 KB memory pages"
int ico_gpu_driver_elf_pages_ok(const void *bytes, size_t n, unsigned pageSize,
                                unsigned long long *minAlign);
/* The same for the file at path (its first 64 KB read); 1 when it cannot be
   read. */
int ico_gpu_driver_file_pages_ok(const char *path, unsigned pageSize);
/* ico_gpu_driver_install_zip_max with the page size of the phone checked
   (pageSize 0 or under 16384: not checked): every file of the package that
   ends in ".so" must pass ico_gpu_driver_elf_pages_ok, else the install is
   ICO_GPU_DRIVER_BAD with ICO_GPU_DRIVER_PAGES_MESSAGE in why and nothing
   left under root. */
int ico_gpu_driver_install_zip_pages(const char *root, const char *zipPath, int apiLevel,
                                     unsigned pageSize, char *folder, size_t folderN, char *why,
                                     size_t whyN);
int ico_gpu_driver_install_zip_pages_max(const char *root, const char *zipPath, int apiLevel,
                                         unsigned pageSize, uint64_t entryMax, char *folder,
                                         size_t folderN, char *why, size_t whyN);

/* What a driver said of itself the last time it ran, kept so the Graphics
   driver page can show it without loading the driver: written at a start
   that made a device (name, version, Mesa version, Vulkan version, driver
   id from RhiLimits), as <root>/<folder>/last_seen.json for a package and
   <root>/last_seen_system.json for the phone's own driver (folder NULL). */
typedef struct IcoGpuDriverSeen {
    int known;        /* 1 once read or filled */
    char name[96];    /* driverName: "turnip Mesa driver" */
    char version[48]; /* the number as its vendor writes it: "512.676.1" */
    char mesa[40];    /* "Mesa 25.3.0-devel" of a Mesa driver, "" otherwise */
    int driverId;     /* VkDriverId, 0 unknown */
    int vulkanMajor, vulkanMinor;
} IcoGpuDriverSeen;

/* 0, or -1 (a bad folder name, the file cannot be written) */
int ico_gpu_driver_seen_write(const char *root, const char *folder, const IcoGpuDriverSeen *s);
/* 1 with *out filled, or 0 with *out zeroed (no file, not JSON, no name) */
int ico_gpu_driver_seen_read(const char *root, const char *folder, IcoGpuDriverSeen *out);
/* The page's words: "Qualcomm's driver 512.676.1, Vulkan 1.3", "Turnip,
   Mesa 25.3.0, Vulkan 1.4", or, for any other driver, its name, its Mesa
   version or version, "Vulkan 1.3" and "driver ID n"; "" when not known.
   (English: the driver's names are.) */
void ico_gpu_driver_seen_text(const IcoGpuDriverSeen *s, char *out, size_t n);

/* A device loss within this many milliseconds of the first present under a
   package counts like a crash inside the driver (the trial marker is written
   again so the next start falls back to the phone's own driver). */
#define ICO_GPU_DRIVER_LOSS_WINDOW_MS 30000ull
/* 1 when a loss sinceFirstPresentMs after the first present (and a package
   was loaded, hadPackage) is such a loss; a loss before any present is
   asked with 0. */
int ico_gpu_driver_loss_counts(int hadPackage, unsigned long long sinceFirstPresentMs);

/* The trial marker <root>/starting.  _begin writes folder into it (0, or
   -1); _ok deletes it; _marker reads it into out (1 when there is one, 0
   with out emptied when not); _crashed is 1 when the marker names folder:
   the last start that tried that driver did not get far enough. */
int ico_gpu_driver_trial_begin(const char *root, const char *folder);
void ico_gpu_driver_trial_ok(const char *root);
int ico_gpu_driver_trial_marker(const char *root, char *out, size_t n);
int ico_gpu_driver_trial_crashed(const char *root, const char *folder);

#endif /* ICO_PLATFORM_GPU_DRIVER_H */
