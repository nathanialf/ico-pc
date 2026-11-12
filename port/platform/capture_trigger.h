/*
 * port/platform/capture_trigger.h
 *
 * A release candidate's frames of the opening (ICO_RC_TOOLS on Android
 * alone; window_capture.h is the window's side): the game's opening
 * sequence (stage 1 with the disc's scene layout in front: menu screen 55
 * on the European disc) plays for about 80 s before the title menu and
 * cannot be paused, so no bug-report picture can be asked for there.  This
 * decides, from the glow's light plan of each picture (rd.h
 * RdPerfRecord.glowLight, RD_GLOW_LIGHT_*), when the window takes the
 * bug-report picture's dump by itself, once a run for each of three
 * moments:
 *   LAMPS  the first LAMPS picture after at least 60 LAMPS pictures in a
 *          row (a settled view lit by the lamps alone)
 *   SUN    the first WHOLE picture after at least 20 WHOLE pictures in a
 *          row (the chain drawn whole: the sun's fans), if any
 *   LATER  the first DARK picture after at least 60 DARK pictures in a row
 *          counted after the LAMPS dump was taken (the opening's later,
 *          cheaper part)
 * A moment met arms the dump for the window's next pump (so the dump never
 * runs inside the picture that met it); dumps are at least 2 s apart
 * (from the end of the one before), never more than 3 a run, and a dump
 * that fails is not tried again.  A picture outside the opening ends every
 * run of pictures and drops an armed dump not yet taken.  A picture with
 * no plan (no glow in the replay, or the plan off) of the same frame as
 * the picture before is a later present of that tick and is passed over;
 * of a new frame, it ends the runs.
 *
 * Pure C (no SDL, no renderer), tested by capture_trigger_test.
 */
#ifndef ICO_PLATFORM_CAPTURE_TRIGGER_H
#define ICO_PLATFORM_CAPTURE_TRIGGER_H

#include <stddef.h>
#include <stdint.h>

#define ICO_CAPTURE_MAX 3                    /* dumps a run at most */
#define ICO_CAPTURE_SPACING_NS 2000000000ull /* 2 s from one dump's end to the next */
#define ICO_CAPTURE_LAMPS_RUN 60u            /* LAMPS pictures in a row before LAMPS */
#define ICO_CAPTURE_SUN_RUN 20u              /* WHOLE pictures in a row before SUN */
#define ICO_CAPTURE_LATER_RUN 60u            /* DARK pictures in a row before LATER */

/* a picture's plan (the window maps RD_GLOW_LIGHT_* onto these) */
enum {
    ICO_CAPTURE_PLAN_NONE, /* no glow, or the plan off: no plan */
    ICO_CAPTURE_PLAN_DARK,
    ICO_CAPTURE_PLAN_LAMPS,
    ICO_CAPTURE_PLAN_WHOLE
};

/* the three moments */
enum { ICO_CAPTURE_LAMPS, ICO_CAPTURE_SUN, ICO_CAPTURE_LATER, ICO_CAPTURE_REASONS };

typedef struct IcoCaptureTrigger {
    int runPlan;        /* the plan of the run of pictures, NONE: no run */
    uint32_t run;       /* its pictures in a row, the last one included */
    uint32_t darkAfter; /* DARK pictures in a row since the LAMPS dump was taken */
    int haveFrame;      /* lastFrame holds the last picture with a plan's frame */
    uint32_t lastFrame;
    unsigned used;     /* bit per moment: its dump taken (or tried) this run */
    int armed;         /* the moment armed for the next pump, -1 none */
    uint32_t armedRun; /* the pictures in a row before the one that armed it */
    int dumps;         /* dumps taken this run */
    int haveLast;      /* lastNs holds the last dump's end */
    uint64_t lastNs;
} IcoCaptureTrigger;

/* A run's start: no picture seen, no dump taken. */
void ico_capture_trigger_init(IcoCaptureTrigger *t);

/* One picture (a replay's record, in order): in the opening or not, its
   plan (ICO_CAPTURE_PLAN_*), its frame number, the time.  Returns the
   moment this picture armed (ICO_CAPTURE_*), or -1. */
int ico_capture_trigger_picture(IcoCaptureTrigger *t, int inOpening, int plan, uint32_t frame,
                                uint64_t nowNs);

/* The window's pump: the armed moment to dump now (it is then used, the
   dump counted, the time noted), or -1: none armed, out of the opening
   (the arm dropped: a later picture may arm the moment again), or busy (a
   picture of an earlier dump still owed: the arm kept). */
int ico_capture_trigger_take(IcoCaptureTrigger *t, int inOpening, int busy, uint64_t nowNs);

/* The dump's end (taken or failed): the 2 s count starts here. */
void ico_capture_trigger_done(IcoCaptureTrigger *t, uint64_t nowNs);

/* The moment's words: "the lamps picture", "the sun picture", "the later
   picture" ("" for no moment). */
const char *ico_capture_trigger_name(int reason);

/* The start line's text (the window prints it after "window: "). */
const char *ico_capture_trigger_start_text(void);

/* The line of a dump taken, without "window: ", into buf (size): the
   moment, the pictures in a row that led to it (t->armedRun as it was
   when it was armed), the dump's number of the run (1..3), the frame
   number, and the zip's file name, or NULL when no zip was written.  The
   length written (at most size - 1). */
size_t ico_capture_trigger_line(char *buf, size_t size, int reason, uint32_t runBefore, int dumpNo,
                                uint32_t frame, const char *zipName);

#endif /* ICO_PLATFORM_CAPTURE_TRIGGER_H */
