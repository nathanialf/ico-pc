/*
 * port/platform/present_fallback.h
 *
 * The FIFO fallback's decision: when presents keep blocking in the driver's
 * present call under mailbox, every later swapchain presents FIFO for the
 * rest of the run (rhi.h rhi_present_fifo_fallback).  Free of SDL and the
 * backend so a unit test replays a log's blocks (present_fallback_test).
 *
 * A present that took ICO_PRESENT_FALLBACK_BLOCK_MS or longer in the
 * driver's present call is a block.  ICO_PRESENT_FALLBACK_COUNT blocks
 * within ICO_PRESENT_FALLBACK_WINDOW_NS switch, once a run, where the
 * window says the switch may be taken (IcoPresentFallbackGate: vsync off,
 * the swapchain in mailbox mode, Vulkan, Qualcomm's own driver, and
 * [dev] present_fallback not off).  Other drivers (a Mali-G52 showed the
 * switch lengthen its blocks) keep mailbox and log the blocks once.  The window (window_host.c) feeds every
 * present over ICO_PRESENT_FALLBACK_SLOW_MS, applies the switch at the
 * step's end and writes the lines this file words.
 */
#ifndef ICO_PLATFORM_PRESENT_FALLBACK_H
#define ICO_PLATFORM_PRESENT_FALLBACK_H

#include <stddef.h>

#define ICO_PRESENT_FALLBACK_BLOCK_MS 200.0
#define ICO_PRESENT_FALLBACK_COUNT 3
#define ICO_PRESENT_FALLBACK_WINDOW_NS 30000000000ull
/* a present over this counts after the switch (the window's own threshold
   for a blocked present) */
#define ICO_PRESENT_FALLBACK_SLOW_MS 50.0
/* the PCI vendor id Qualcomm's Adreno driver reports */
#define ICO_PRESENT_FALLBACK_QCOM_VENDOR 0x5143u

/* [dev] present_fallback (ICO_PRESENT_FALLBACK, host_config.c) */
typedef enum IcoPresentFallbackMode {
    ICO_PRESENT_FALLBACK_AUTO = 0, /* the default: FIFO after the blocks, and from the start on
                                      Qualcomm's own Android driver with vsync off */
    ICO_PRESENT_FALLBACK_OFF,      /* mailbox kept whatever the blocks */
    ICO_PRESENT_FALLBACK_FIFO      /* FIFO from the start */
} IcoPresentFallbackMode;

/* What the window says about the present it notes */
typedef enum IcoPresentFallbackGate {
    ICO_PRESENT_FALLBACK_GATE_NO = 0, /* no switch: vsync on, not mailbox, not Vulkan, Mesa */
    ICO_PRESENT_FALLBACK_GATE_YES,    /* the switch may be taken */
    ICO_PRESENT_FALLBACK_GATE_KEPT,   /* it could, but [dev] present_fallback = off */
    ICO_PRESENT_FALLBACK_GATE_OTHER   /* every condition holds but the vendor: not Qualcomm's own
                                         driver, where the blocks were measured; mailbox kept */
} IcoPresentFallbackGate;

/* ico_present_fallback_note's answers */
#define ICO_PRESENT_FALLBACK_SWITCH 1 /* switch to FIFO now (once a run) */
#define ICO_PRESENT_FALLBACK_KEPT 2 /* the count was reached and the dev key kept mailbox (once) */
#define ICO_PRESENT_FALLBACK_OTHER                                                                 \
    3 /* the count was reached on a driver that is not Qualcomm's own
                                        (GATE_OTHER): mailbox kept (once) */

/* Zero-initialise before the first note. */
typedef struct IcoPresentFallback {
    unsigned long long atNs[ICO_PRESENT_FALLBACK_COUNT]; /* the latest blocks, oldest first */
    double ms[ICO_PRESENT_FALLBACK_COUNT];
    int n;                                     /* how many of them are held */
    unsigned blocks;                           /* blocks the run had */
    int lastGate;                              /* the gate of the latest note */
    int taken;                                 /* the switch was answered (once a run) */
    int kept;                                  /* the kept answer was given (once a run) */
    unsigned long long takenNs;                /* when (the switch, or the kept answer) */
    unsigned takenBlocks;                      /* blocks the run had at that note */
    double takenMinMs, takenMaxMs, takenSpanS; /* the counted blocks then */
    unsigned slowSince; /* presents over ICO_PRESENT_FALLBACK_SLOW_MS since the switch */
} IcoPresentFallback;

/* [dev] present_fallback's value (the environment's, NULL when unset):
   "off", "0", "false", "no" off; "fifo" FIFO from the start; anything else
   (unset, "auto") auto */
IcoPresentFallbackMode ico_present_fallback_mode(const char *value);

/* Whether the driver may take the switch: a device (vendorId not 0) whose
   driver text names neither "Mesa" nor "turnip" (any case).  Mesa's drivers
   (Turnip on the same Adreno) block far less under mailbox; a few blocks of
   230 ms there are not the stock driver's queue wait. */
int ico_present_fallback_driver_eligible(unsigned vendorId, const char *driverText);

/* Qualcomm's own driver: vendorId 0x5143 (ICO_PRESENT_FALLBACK_QCOM_VENDOR)
   and an eligible driver text (neither Mesa nor turnip).  The rhi calls it
   (rhi_present_fifo_start) when it makes a swapchain, before any present. */
int ico_present_fallback_qualcomm_own(unsigned vendorId, const char *driverText);

/* The window's gate for the present it notes (window_present_fallback.c):
   GATE_NO with vsync on, a swapchain not in mailbox mode, a backend other
   than Vulkan, a driver that is not eligible (no device, Mesa, Turnip) or
   mode fifo (FIFO from the start needs no switch); GATE_KEPT under mode
   off; GATE_YES on Qualcomm's own driver; GATE_OTHER on every other
   eligible driver (Mali, PowerVR, the desktop cards' own drivers: the
   switch was measured on Qualcomm's own driver only, and on a Mali-G52 it
   lengthened the blocks, issue 179). */
IcoPresentFallbackGate ico_present_fallback_gate(int vsync, int mailbox, int vulkan,
                                                 unsigned vendorId, const char *driverText,
                                                 IcoPresentFallbackMode mode);

/* 1 in the Android build (the phones FIFO from the start is for), 0
   elsewhere: the platform argument below */
#ifdef __ANDROID__
#define ICO_PRESENT_FALLBACK_ANDROID 1
#else
#define ICO_PRESENT_FALLBACK_ANDROID 0
#endif

/* Whether the window hands the rhi the driver question
   (rhi_present_fifo_start with ico_present_fallback_qualcomm_own): mode
   auto on Android only.  Windows on Snapdragon reports the same vendor id
   with Qualcomm's own Vulkan driver, and there the surface offers
   immediate mode, so Vsync Off was immediate; FIFO from the start would
   make it act as on */
int ico_present_fallback_start_asked(IcoPresentFallbackMode mode, int android);

/* Whether the first swapchain presents FIFO at the driver's fewest images:
   the question asked (mode auto on Android: not off, which keeps mailbox
   for the whole run; fifo is FIFO from the start whatever the driver, a
   path of its own), vsync off, and Qualcomm's own driver.  Qualcomm's Adreno guide advises FIFO with the
   minimum image count and says mailbox costs battery and heat for no
   benefit; with it no mailbox to FIFO switch happens mid-run and the GPU
   draws nothing that is never shown.  It is not known to remove the
   stalls issue 123 reports (they continued under FIFO in three logs). */
int ico_present_fallback_start(IcoPresentFallbackMode mode, int android, int vsync,
                               unsigned vendorId, const char *driverText);

/* The start line, once, after the first swapchain when
   ico_present_fallback_start said yes: "window: present fifo (Qualcomm's
   own driver: FIFO from the start; [dev] present_fallback = off keeps
   mailbox)", no newline. */
void ico_present_fallback_start_line(char *out, size_t size);

/* One present's time in the driver's present call (presentMs) at nowNs
   with the window's gate: a block (>= ICO_PRESENT_FALLBACK_BLOCK_MS) joins
   the count, the ones older than ICO_PRESENT_FALLBACK_WINDOW_NS before it
   leave.  ICO_PRESENT_FALLBACK_SWITCH exactly once a run, when the count
   reaches ICO_PRESENT_FALLBACK_COUNT under GATE_YES; ICO_PRESENT_FALLBACK_KEPT
   once, when it reaches it under GATE_KEPT before any switch;
   ICO_PRESENT_FALLBACK_OTHER once, when it reaches it under GATE_OTHER
   (the same once as KEPT); else 0. */
int ico_present_fallback_note(IcoPresentFallback *f, unsigned long long nowNs, double presentMs,
                              int gate);

/* The switch's line, after ICO_PRESENT_FALLBACK_SWITCH:
   "window: present blocked 3 times (376-382 ms) in 14 s on <adapter>
   (<driver text's first line>) under mailbox (vsync off, 6 images):
   presents now FIFO for this run ([dev] present_fallback=off keeps
   mailbox)", no newline.  images: the swapchain's count before the switch. */
void ico_present_fallback_switch_line(const IcoPresentFallback *f, const char *adapter,
                                      const char *driverText, unsigned images, char *out,
                                      size_t size);

/* The kept line, after ICO_PRESENT_FALLBACK_KEPT: "window: present blocked
   3 times over 200 ms in 21 s under mailbox: mailbox kept ([dev]
   present_fallback=off)", no newline. */
void ico_present_fallback_kept_line(const IcoPresentFallback *f, char *out, size_t size);

/* The other-driver line, after ICO_PRESENT_FALLBACK_OTHER: "window: present
   blocked 3 times (204-240 ms) in 8 s on <adapter> (<driverText's first
   line>, which already reads "device 13b5:74021000; driver Mali-G52 ...")
   under mailbox: mailbox kept (presents go FIFO by themselves
   only on Qualcomm's own driver, where the blocks were measured; [dev]
   present_fallback = fifo asks for FIFO here)", no newline. */
void ico_present_fallback_other_line(const IcoPresentFallback *f, const char *adapter,
                                     const char *driverText, char *out, size_t size);

/* The 10 s block line's suffix at nowNs: "; K since presents went FIFO N s
   ago" after the switch (K: presents over ICO_PRESENT_FALLBACK_SLOW_MS
   since), "; k of 3 over 200 ms in 30 s toward FIFO" before it while the
   latest gate was GATE_YES (never under GATE_OTHER) and k blocks are within the window, else "". */
void ico_present_fallback_block_suffix(const IcoPresentFallback *f, unsigned long long nowNs,
                                       char *out, size_t size);

/* The present mode line's and the summary's suffix: ", FIFO fallback taken
   after N blocks" after the switch, ", FIFO fallback set by [dev]
   present_fallback=fifo" under mode FIFO, ", FIFO from the start
   (Qualcomm's own driver)" when fifoStart (ico_present_fallback_start said
   yes and the swapchain is FIFO), ", FIFO fallback armed" when armed (the
   gate would be GATE_YES), else "". */
void ico_present_fallback_mode_suffix(const IcoPresentFallback *f, IcoPresentFallbackMode mode,
                                      int armed, int fifoStart, char *out, size_t size);

#endif /* ICO_PLATFORM_PRESENT_FALLBACK_H */
