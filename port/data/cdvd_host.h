/*
 * port/data/cdvd_host.h
 *
 * The host side of the libcdvd subset fumi/ios/cdvd.c and seki/src/
 * FileManager.c call (port/compat/libcdvd.h declares the sceCd* entry
 * points; port/data/cdvd_host.c defines them over the VFS).  This header is
 * the port's own control surface: which disc image is in the drive, the
 * simulated-vsync completion of non-blocking reads, and the clock.
 */
#ifndef ICO_PORT_CDVD_HOST_H
#define ICO_PORT_CDVD_HOST_H

#include "vfs.h"

/* libcdvd's values the host reproduces */
#define ICO_CD_READY_COMPLETE 2  /* sceCdDiskReady: SCECdComplete */
#define ICO_CD_READY_NOT_READY 6 /* sceCdDiskReady: SCECdNotReady */
#define ICO_CD_STAT_STOP 0x00    /* sceCdStatus: the spindle is stopped (no disc) */
#define ICO_CD_STAT_PAUSE 0x0A   /* sceCdStatus: SCECdStatPause, the idle state */
#define ICO_CD_TYPE_NODISC 0x00  /* sceCdGetDiskType: SCECdNODISC */
#define ICO_CD_TYPE_PS2CD 0x12   /* sceCdGetDiskType: SCECdPS2CD (the Japanese cdvd.c's 18) */
#define ICO_CD_TYPE_PS2DVD 0x14  /* sceCdGetDiskType: SCECdPS2DVD (cdvd.c's cdDiskType 20) */
#define ICO_CD_ERR_NONE 0x00
#define ICO_CD_ERR_ABORT 0x01   /* the command was broken off (sceCdBreak) */
#define ICO_CD_ERR_NODISC 0x12  /* FileManager.c: "No Disc." */
#define ICO_CD_ERR_ADDRESS 0x20 /* FileManager.c: "Invalid transfer address." */
#define ICO_CD_ERR_READ 0x30    /* FileManager.c: "Read error." */
#define ICO_CD_ERR_END 0x32     /* FileManager.c: "Reach to CD end." */

/* Put a disc image in the drive: mount `iso_path` with the ISO9660 backend
   and make it the VFS disc.  Replaces (and unmounts) a disc this layer
   mounted earlier.  0, or -1 if the image cannot be mounted. */
int ico_cdvd_host_mount_iso(const char *iso_path);

/* Take out a disc this layer mounted. */
void ico_cdvd_host_eject(void);

/* The disc image sceCdInit loads when no disc is in the drive yet: the
   ICO_ISO environment variable, else baserom/Ico_PAL.iso under the working
   directory (dev mode; a normal start hands the extracted archive in
   through ico_vfs_set_disc instead). */
const char *ico_cdvd_host_default_iso(void);

/* The boot file SYSTEM.CNF's BOOT2 line names, without device and version
   ("SCES_507.60"), or NULL when the disc has none.  sceCdGetDiskType
   reports a PS2 disc only when this file exists on the disc: the type of
   its ico_discs row (archive.h; a CD for SCPS_110.03), else a DVD. */
const char *ico_cdvd_host_boot_name(void);

/* Complete the non-blocking command in flight.  Registered as a simulated
   vsync callback when the host loop provides one (port/platform/
   host_loop.c); tests call it directly. */
void ico_cdvd_host_vsync(void *ctx);

/* Nonzero while a non-blocking command waits for the next vsync. */
int ico_cdvd_host_busy(void);

/* The drive's counters, for the diagnostics heartbeat (port/platform/
   diag_host.c). */
typedef struct IcoCdvdStats {
    int busy;              /* a command waits for the next vsync */
    int waiters;           /* threads blocked in sceCdSync(0) */
    unsigned int reads;    /* read commands started */
    unsigned int sectors;  /* sectors read */
    unsigned int last_lsn; /* the last command's first sector */
    int stream_active;     /* sceCdStStart'ed */
    unsigned int stream_lsn;
} IcoCdvdStats;

void ico_cdvd_host_stats(IcoCdvdStats *out);

/* The game's background reader (ico2/fumi/ios/cdvd.c) is about to retry a
   read of `name` at `lsn`: `error` 0 when the drive was not ready during
   it, else sceCdGetError's code.  Logged (`cdvd: background read of ...
   retried`) for the first 8 retries and then every 100th. */
void ico_cdvd_host_bg_retry(const char *name, unsigned int lsn, int error);

/* The game's pack loader (ico2/fumi/ios/cdvd.c, iosCdvdMgrPackLoad) has read
   the header of the pack `name` (the disc's spelling, "\STGST13C.DF;1"):
   its first sector, its bytes on the disc, the members its header counts and
   whether it is read through the game's inflate.  Logged (`cdvd: pack ...`)
   once per load, a stage change's event, so a failure during or right after
   a load names the pack; the first 64 loads, then every 100th. */
void ico_cdvd_host_pack_load(const char *name, unsigned int lsn, unsigned int size, int members,
                             int inflating);

/* Whether sceCdInit registered ico_cdvd_host_vsync with the host loop. */
int ico_cdvd_host_vsync_hooked(void);

/* A test's source for the clock sceCdReadClock reports, in decimal.  Without
   one (the default) sceCdReadClock reads the port clock (ico_clock_now,
   port/platform/clock.c: the host's local time, or a fixed moment when
   ICO_FIXED_CLOCK is set, so runs stay reproducible). */
typedef struct IcoCdClockTime {
    int year; /* four digits */
    int month;
    int day;
    int hour;
    int minute;
    int second;
} IcoCdClockTime;

typedef void (*IcoCdClockFn)(IcoCdClockTime *out);

void ico_cdvd_host_set_clock_source(IcoCdClockFn fn);

/* Forget all drive state (tests). */
void ico_cdvd_host_reset(void);

#endif /* ICO_PORT_CDVD_HOST_H */
