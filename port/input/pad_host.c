/*
 * port/input/pad_host.c
 *
 * libpad for the host build (docs/port/INPUT.md): one DualShock 2 in port 0
 * slot 0 when something feeds it, nothing otherwise.
 *
 * Feeds, in priority order: the pad script (port/input/pad_script.h, the
 * headless and test source), then the live virtual pad the binding layer
 * fills each vsync (gamepad, keyboard and mouse, already merged there). With
 * neither, no controller is in either port: fumi/ios/pad.c's
 * controler_stable_check sees scePadGetState return 0, keeps the port's
 * error flag set and never calls scePadRead, and the game runs as a console
 * with no pad plugged in (the headless default).
 *
 * The pad powers up in digital mode, as the real one did, and walks
 * pad.c's state machine (controler_stable_check, pad.c:80-270) once per
 * Main tick while the port is in error. The answers are those the scripted
 * pad has always given, so the headless traces do not change:
 *
 *   tick 0  phase 0/1: state 6 (stable); InfoMode(CURID) 4 (digital),
 *           InfoMode(CUREXID) 0, so mode 4 = orig: phase 40, flag 0x40000
 *   tick 1  phase 40: InfoMode(IDTABLE, -1) 2 modes; 41: SetMainMode(1, 3)
 *           (analog, locked) accepted: phase 42
 *   tick 2  phase 42: GetReqState 0 (complete): phase 1
 *   tick 3  phase 1: InfoMode(CURID) 7 (analog): phase 70
 *   tick 4  phase 70: InfoPressMode 0: phase 75
 *   tick 5  phase 75: InfoAct(-1) 2 motors; SetActAlign accepted: phase 76
 *   tick 6  phase 76: state 6, not 5 (executing): phase 99
 *   tick 7  iosPadDevReadFunc clears the error at phase 99 (pad.c:317-319)
 *           and reads: id 0x73 (analog, (word >> 12) & 0xF == 7, so the
 *           sticks are kept, pad.c:335-339)
 *
 * The first buttons reach the game on Main tick 7; there pad.c compares
 * against the other, never-filled buffer (all zero, which reads as every
 * button held), so tick 7 triggers nothing and releases every button not
 * held, as the PS2 did on the first read after a pad came up. Script
 * presses should start at tick 8 or later.
 *
 * InfoPressMode answers 0: the game never reads the pressure bytes
 * (fumi/include/pad.h's IosPadBuf stops at the sticks), so the pad is not
 * walked into pressure mode. scePadEnterPressMode still works for a caller
 * that asks (the buffer then has id 0x79 and the 12 pressure bytes).
 */
#include <libpad.h>
#include <string.h>
#include "input.h"
#include "input_record.h"
#include "options.h"

#define PAD_STATE_DISCONNECTED 0 /* scePadStateDiscon */
#define PAD_STATE_STABLE 6       /* scePadStateStable (pad.c's "STABLE") */
#define PAD_REQ_COMPLETE 0       /* scePadReqStateComplete */
/* scePadInfoMode's term argument */
#define PAD_INFO_CURID 1
#define PAD_INFO_CUREXID 2
#define PAD_INFO_CUREXOFFS 3
#define PAD_INFO_IDTABLE 4
/* terminal ids: the upper nibble of scePadRead's byte 1 */
#define PAD_ID_DIGITAL 4 /* 0x41 */
#define PAD_ID_ANALOG 7  /* 0x73, DualShock */
#define PAD_READ_SIZE 32 /* bytes scePadRead fills */

static int s_live;
static IcoVirtualPad s_vpad;
/* the pad's mode: digital until scePadSetMainMode(1) */
static int s_analog;
static int s_press;
static unsigned char s_align[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static unsigned short s_rumble_high, s_rumble_low;

void ico_input_set_live(int on)
{
    s_live = on != 0;
    if (!s_live) {
        memset(&s_vpad, 0, sizeof(s_vpad));
        s_rumble_high = s_rumble_low = 0;
    }
}

int ico_input_live(void)
{
    return s_live;
}

void ico_input_set_vpad(const IcoVirtualPad *v)
{
    s_vpad = *v;
}

/* stick fix and mirror live in port/game/options.c (docs/port/OPTIONS.md) */
void ico_input_set_stick_fix(int on)
{
    ico_opt_set_stick_fix(on);
}

int ico_input_stick_fix_enabled(void)
{
    return ico_opt_stick_fix();
}

void ico_input_set_mirror(int on)
{
    ico_opt_set_mirror(on);
}

int ico_input_mirror(void)
{
    return ico_opt_mirror();
}

void ico_input_frame(IcoPadFrame *out)
{
    if (ico_pad_script_active()) {
        ico_pad_script_frame(out);
    } else if (s_live) {
        ico_input_vpad_to_frame(&s_vpad, ico_opt_stick_fix(), ico_opt_mirror(), out);
    } else {
        memset(out, 0, sizeof(*out));
        out->lx = out->ly = out->rx = out->ry = ICO_PAD_STICK_CENTRE;
    }
}

void ico_pad_rumble_map(const unsigned char align[6], const unsigned char act[6],
                        unsigned short *high_freq, unsigned short *low_freq)
{
    int i;

    *high_freq = 0;
    *low_freq = 0;
    for (i = 0; i < 6; i++) {
        if (align[i] == 0) {
            *high_freq = act[i] != 0 ? 0xFFFFu : 0;
        } else if (align[i] == 1) {
            *low_freq = (unsigned short)(act[i] * 257u);
        }
    }
}

void ico_input_rumble_get(unsigned short *high_freq, unsigned short *low_freq)
{
    *high_freq = s_rumble_high;
    *low_freq = s_rumble_low;
}

static int connected(int port, int slot)
{
    return port == 0 && slot == 0 && (ico_pad_script_active() || s_live);
}

int scePadInit(int a0)
{
    (void)a0;
    s_analog = 0;
    s_press = 0;
    memset(s_align, 0xFF, sizeof(s_align));
    s_rumble_high = s_rumble_low = 0;
    return 1;
}

int scePadInit2(int a0)
{
    (void)a0;
    return 1;
}

int scePadGetModVersion(void)
{
    return 0;
}

int scePadPortOpen(int port, int slot, void *addr)
{
    (void)port;
    (void)slot;
    (void)addr;
    return 1;
}

int scePadGetState(int port, int slot)
{
    return connected(port, slot) ? PAD_STATE_STABLE : PAD_STATE_DISCONNECTED;
}

int scePadGetReqState(int port, int slot)
{
    (void)port;
    (void)slot;
    return PAD_REQ_COMPLETE;
}

/* Disconnected: returns 0 and leaves a released, centred frame in the
   buffer for callers that read it anyway (motionViewer.c ignores the
   result): status 0xFF (no data), digital id 0x41, every button up (active
   low), sticks centred.  Connected, SCE libpad's layout:
     0     status, 0 = ok
     1     terminal id: 0x41 digital, 0x73 analog, 0x79 analog + pressure
     2,3   buttons, active low: byte 2 the high byte, byte 3 the low byte of
           the logical word (fumi/include/pad.h:15-24)
     4-7   right x, right y, left x, left y (analog ids only; digital mode
           leaves them centred)
     8-19  pressure, 0x79 only: right left up down triangle circle cross
           square L1 R1 L2 R2, 0 or 255 (the sources are digital)
   The rest of the 32 bytes is zero. */
int scePadRead(int port, int slot, void *data)
{
    unsigned char *d = data;
    IcoPadFrame f;

    if (!connected(port, slot)) {
        if (d != NULL) {
            memset(d, 0, PAD_READ_SIZE);
            d[0] = 0xFF;
            d[1] = 0x41;
            d[2] = 0xFF;
            d[3] = 0xFF;
            d[4] = d[5] = d[6] = d[7] = 0x80;
        }
        return 0;
    }
    ico_input_frame(&f);
    /* package Q1: what this tick read, for the pad recording */
    ico_input_record_sample(ico_pad_script_tick(), &f);
    if (d != NULL) {
        unsigned int raw = ~f.buttons & 0xFFFFu;

        memset(d, 0, PAD_READ_SIZE);
        d[0] = 0x00;
        d[1] = s_analog ? (s_press ? 0x79 : 0x73) : 0x41;
        d[2] = (unsigned char)(raw >> 8);
        d[3] = (unsigned char)raw;
        d[4] = d[5] = d[6] = d[7] = ICO_PAD_STICK_CENTRE;
        if (s_analog) {
            static const unsigned int order[12] = {ICO_PAD_RIGHT, ICO_PAD_LEFT,     ICO_PAD_UP,
                                                   ICO_PAD_DOWN,  ICO_PAD_TRIANGLE, ICO_PAD_CIRCLE,
                                                   ICO_PAD_CROSS, ICO_PAD_SQUARE,   ICO_PAD_L1,
                                                   ICO_PAD_R1,    ICO_PAD_L2,       ICO_PAD_R2};
            int i;

            d[4] = f.rx;
            d[5] = f.ry;
            d[6] = f.lx;
            d[7] = f.ly;
            if (s_press) {
                for (i = 0; i < 12; i++) {
                    d[8 + i] = (f.buttons & order[i]) != 0 ? 0xFF : 0;
                }
            }
        }
    }
    return PAD_READ_SIZE;
}

int scePadInfoMode(int port, int slot, int term, int index)
{
    if (!connected(port, slot)) {
        return 0;
    }
    switch (term) {
    case PAD_INFO_CURID:
        return s_analog ? PAD_ID_ANALOG : PAD_ID_DIGITAL;
    case PAD_INFO_CUREXID:
    case PAD_INFO_CUREXOFFS:
        return 0; /* no extended mode */
    case PAD_INFO_IDTABLE:
        if (index == -1) {
            return 2;
        }
        return index == 0 ? PAD_ID_DIGITAL : index == 1 ? PAD_ID_ANALOG : 0;
    default:
        return 0;
    }
}

/* act -1 asks for the number of actuators: two motors.  The per-motor
   queries are not made by the game. */
int scePadInfoAct(int port, int slot, int act, int term)
{
    (void)term;
    if (!connected(port, slot)) {
        return 0;
    }
    return act == -1 ? 2 : 0;
}

int scePadInfoPressMode(int port, int slot)
{
    (void)port;
    (void)slot;
    return 0;
}

int scePadEnterPressMode(int port, int slot)
{
    if (!connected(port, slot)) {
        return 0;
    }
    s_press = 1;
    return 1;
}

int scePadGetButtonMask(int port, int slot)
{
    (void)port;
    (void)slot;
    return 0;
}

/* mode 1 is analog, 0 digital; the request completes at once
   (scePadGetReqState always reports complete). */
int scePadSetMainMode(int port, int slot, int mode, int option)
{
    (void)option;
    if (!connected(port, slot)) {
        return 0;
    }
    s_analog = mode == 1;
    return 1;
}

int scePadSetActAlign(int port, int slot, char *act)
{
    if (!connected(port, slot)) {
        return 0;
    }
    if (act != NULL) {
        memcpy(s_align, act, sizeof(s_align));
    }
    return 1;
}

int scePadSetActDirect(int port, int slot, unsigned char *act)
{
    if (!connected(port, slot)) {
        return 0;
    }
    if (act != NULL && !ico_pad_script_active()) {
        ico_pad_rumble_map(s_align, act, &s_rumble_high, &s_rumble_low);
    }
    return 1;
}
