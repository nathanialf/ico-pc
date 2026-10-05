/*
 * port/compat/sifdev.h
 *
 * The host build's sifdev.h: the declarations the game uses, from
 * sce/libkernl/sifdev.h (this project's own clean-room header, MIT), with
 * pointer-sized and 64-bit types made host-correct.
 */
#ifndef ICO_COMPAT_SIFDEV_H
#define ICO_COMPAT_SIFDEV_H

int sceClose(unsigned int fd);
int sceLseek(unsigned int fd, int offset, int whence);
int sceOpen(unsigned char *name, int flags, ...);
int sceRead(int fd, void *buf, int nbyte);
int sceWrite(int fd, void *buf, int nbyte);

#endif /* ICO_COMPAT_SIFDEV_H */
