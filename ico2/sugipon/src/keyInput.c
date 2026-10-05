#include "debug.h"
#include <eekernel.h>
#include "keyInput.h"
#include "main.h"
#include "pad.h"

/* the pad device descriptor InitKeyInput hands to iosPadDevInit */
static int keyInputPadDev[6] = {7, 2, 0, 0, 0, 0}; /* derived name */

void InitKeyInput(int unused)
{
    int i;
    int j;

    debug_StdPrintfDummy("InitKeyInput2() in\n");
    debug_StdPrintfDummy("PadInit\n");
    iosPadDevInit(keyInputPadDev);
    for (i = 0; i < 2; i++) {
        pad[i].old = 0;
        pad[i].flags = 0;
        pad[i].rel = 0;
        pad[i].rep = 0;
        for (j = 15; j >= 0; j--) {
            pad[i].hist[j] = 0;
        }
    }
    debug_StdPrintfDummy("InitKeyInput2() out\n");
    debug_StdPrintfDummy("signal to main\n");
    SignalSema(IosPadLock);
}

void ExecKeyInput(void)
{
    IosPadCtx buf;
    IosPadStick stR;
    IosPadStick stL;
    int i;
    unsigned int j;

    iosPadDevRead();
    for (i = 0; i < 2; i++) {
        pad[i].old = pad[i].now;
        iosPadConnect(&buf, 7, i, &iosPadConfDefault);
        iosPadRead(&buf);
        pad[i].now = buf.now2;
        pad[i].flags = buf.trg2;
        pad[i].rel = buf.rel2;
        pad[i].rep = 0;
        iosPadGetStick(&buf, &stL, 1, 127, 127, 0);
        pad[i].ana[0] = stL.x;
        pad[i].ana[1] = stL.y;
        iosPadGetStick(&buf, &stR, 0, 127, 127, 0);
        pad[i].ana[2] = stR.x;
        pad[i].ana[3] = stR.y;
        for (j = 0; j < 16; j++) {
            if ((pad[i].now >> j) & 1) {
                pad[i].hist[j]++;
            } else {
                pad[i].hist[j] = 0;
            }
            if (pad[i].hist[j] == 1 ||
                (float)pad[i].hist[j] >
                    (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 20.0f) {
                pad[i].rep |= 1 << j;
            } else {
                pad[i].rep &= ~(1 << j);
            }
        }
    }
}
