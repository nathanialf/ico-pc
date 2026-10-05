/*
 * port/null/pad_null.c
 *
 * libpad with no controller in either port.  fumi/ios/pad.c's
 * controler_stable_check sees scePadGetState return 0 (the library's
 * "disconnected" state), keeps the port's error flag set and never calls
 * scePadRead, and iosPadRead hands the game zero buttons: the game runs as
 * a console with no pad plugged in.  The library calls themselves succeed
 * (scePadInit, scePadPortOpen), as they did with empty ports.
 *
 * port/input/ replaces this in Phase 4.
 */
#include <libpad.h>

#include <string.h>

#define PAD_STATE_DISCONNECTED 0 /* scePadStateDiscon */
#define PAD_REQ_COMPLETE 0       /* scePadReqStateComplete */

int scePadInit(int a0)
{
    (void)a0;
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
    (void)port;
    (void)slot;
    return PAD_STATE_DISCONNECTED;
}

int scePadGetReqState(int port, int slot)
{
    (void)port;
    (void)slot;
    return PAD_REQ_COMPLETE;
}

/* No data: returns 0, and leaves a released, centred frame in the buffer
   for callers that read it anyway (motionViewer.c ignores the result):
   status 0xFF (no data), digital id 0x41, every button up (active low),
   sticks centred. */
int scePadRead(int port, int slot, void *data)
{
    unsigned char *d = data;

    (void)port;
    (void)slot;
    if (d != NULL) {
        memset(d, 0, 32);
        d[0] = 0xFF;
        d[1] = 0x41;
        d[2] = 0xFF;
        d[3] = 0xFF;
        d[4] = d[5] = d[6] = d[7] = 0x80;
    }
    return 0;
}

int scePadInfoMode(int port, int slot, int term, int index)
{
    (void)port;
    (void)slot;
    (void)term;
    (void)index;
    return 0;
}

int scePadInfoAct(int port, int slot, int act, int term)
{
    (void)port;
    (void)slot;
    (void)act;
    (void)term;
    return 0;
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

int scePadSetMainMode(int port, int slot, int mode, int option)
{
    (void)port;
    (void)slot;
    (void)mode;
    (void)option;
    return 0;
}

int scePadSetActAlign(int port, int slot, char *act)
{
    (void)port;
    (void)slot;
    (void)act;
    return 0;
}

int scePadSetActDirect(int port, int slot, unsigned char *act)
{
    (void)port;
    (void)slot;
    (void)act;
    return 0;
}
