/*
 * port/compat/libpad.h
 *
 * The host build's libpad.h: the declarations the game uses, from
 * sce/libpad/libpad.h (this project's own clean-room header, MIT), with
 * pointer-sized and 64-bit types made host-correct.
 */
#ifndef ICO_COMPAT_LIBPAD_H
#define ICO_COMPAT_LIBPAD_H

int scePadEnterPressMode(int port, int slot);
int scePadGetButtonMask(int port, int slot);
int scePadGetModVersion(void);
int scePadGetReqState(int port, int slot);
int scePadGetState(int port, int slot);
int scePadInfoAct(int port, int slot, int act, int term);
int scePadInfoMode(int port, int slot, int term, int index);
int scePadInfoPressMode(int port, int slot);
int scePadInit(int a0);
int scePadInit2(int a0);
int scePadPortOpen(int port, int slot, void *addr);
int scePadRead(int port, int slot, void *data);
int scePadSetActAlign(int port, int slot, char *act);
int scePadSetActDirect(int port, int slot, unsigned char *act);
int scePadSetMainMode(int port, int slot, int mode, int option);

#endif /* ICO_COMPAT_LIBPAD_H */
