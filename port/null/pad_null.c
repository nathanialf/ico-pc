/*
 * port/null/pad_null.c
 *
 * libpad for the headless build: no controller, or a scripted DualShock.
 *
 * No script (the default): no controller in either port.  fumi/ios/pad.c's
 * controler_stable_check sees scePadGetState return 0 (the library's
 * "disconnected" state), keeps the port's error flag set and never calls
 * scePadRead, and iosPadRead hands the game zero buttons: the game runs as
 * a console with no pad plugged in.  The library calls themselves succeed
 * (scePadInit, scePadPortOpen), as they did with empty ports.
 *
 * With a script (port/input/pad_script.h, --pad-script): port 0 slot 0 has
 * a DualShock (analog, two motors, no pressure-sensitive mode) that powers
 * up in digital mode, as the real pad did; port 1 stays empty.  pad.c's
 * state machine (controler_stable_check, pad.c:80-270) is called once per
 * Main tick while the port is in error and walks, with the answers below:
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
 *           sticks are kept, pad.c:335-339), the script's buttons
 *
 * So the first scripted buttons reach the game on Main tick 7; before that
 * iosPadRead reports nothing.  On that first read pad.c compares against
 * the other, never-filled buffer (all zero, which reads as every button
 * held), so tick 7 triggers nothing (a button held since earlier shows as
 * held, never as pressed) and releases every button not held, as the PS2
 * did on the first read after a pad came up.  Script presses should start
 * at tick 8 or later.
 *
 * port/input/ replaces this in Phase 4.
 */
#include <libpad.h>
#include <string.h>
#include "pad_script.h"

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
#define PAD_READ_SIZE 32 /* bytes scePadRead fills; pad.c only tests for 0 */

/* the scripted DualShock's mode: digital until scePadSetMainMode(1) */
static int analog;

static int connected(int port, int slot)
{
    return port == 0 && slot == 0 && ico_pad_script_active();
}

int scePadInit(int a0)
{
    (void)a0;
    analog = 0;
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
   low), sticks centred.  Connected: status 0, id 0x41 or 0x73, the
   script's buttons active low (byte 2 the high byte, byte 3 the low byte of
   the logical word, pad.h:15-24), sticks rx ry lx ly at bytes 4-7 (analog
   only; digital mode leaves them centred). */
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
    ico_pad_script_frame(&f);
    if (d != NULL) {
        unsigned int raw = ~f.buttons & 0xFFFFu;

        memset(d, 0, PAD_READ_SIZE);
        d[0] = 0x00;
        d[1] = analog ? 0x73 : 0x41;
        d[2] = (unsigned char)(raw >> 8);
        d[3] = (unsigned char)raw;
        d[4] = d[5] = d[6] = d[7] = ICO_PAD_STICK_CENTRE;
        if (analog) {
            d[4] = f.rx;
            d[5] = f.ry;
            d[6] = f.lx;
            d[7] = f.ly;
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
        return analog ? PAD_ID_ANALOG : PAD_ID_DIGITAL;
    case PAD_INFO_CUREXID:
    case PAD_INFO_CUREXOFFS:
        return 0; /* no extended mode (a DualShock, not a DualShock 2) */
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
    (void)port;
    (void)slot;
    return 0;
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
    analog = mode == 1;
    return 1;
}

int scePadSetActAlign(int port, int slot, char *act)
{
    (void)act;
    return connected(port, slot) ? 1 : 0;
}

int scePadSetActDirect(int port, int slot, unsigned char *act)
{
    (void)act;
    return connected(port, slot) ? 1 : 0;
}
