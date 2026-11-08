/*
 * port/compat/eekernel.h
 *
 * The host build's eekernel.h: the EE kernel calls the game makes, with the
 * signatures of sce/libkernl/eekernel.h (this project's own clean-room
 * header, MIT), adapted for the host: the 64-bit GS registers are
 * `unsigned long long` (the EE's `long` is 64 bits; the host's may be 32).
 * The definitions are port/platform/kernel_host.c, over the fiber scheduler
 * (sched.c).
 * Only what the game uses is declared.
 */
#ifndef ICO_COMPAT_EEKERNEL_H
#define ICO_COMPAT_EEKERNEL_H

/* The kernel's parameter records. Field order follows the EE kernel ABI;
   on the host only the field names matter (the game never hands these to
   anything but the kernel). */
struct SemaParam {
    int currentCount;
    int maxCount;
    int initCount;
    int numWaitThreads;
    unsigned int attr;
    unsigned int option;
};

struct ThreadParam {
    int status;
    void (*entry)(void *);
    void *stack;
    int stackSize;
    void *gpReg;
    int initPriority;
    int currentPriority;
    unsigned int attr;
    unsigned int option;
    int waitType;
    int waitId;
    int wakeupCount;
};

/* Interrupt control. */
int AddIntcHandler(int cause, int (*handler)(int cause), int next);
int RemoveIntcHandler(int cause, int id);
int AddDmacHandler(int channel, int (*handler)(int channel), int next);
int RemoveDmacHandler(int channel, int id);
int EnableIntc(int cause);
int DisableIntc(int cause);
int EnableDmac(int channel);
int DisableDmac(int channel);
int _EnableIntc(int cause);
int _DisableIntc(int cause);
int _EnableDmac(int channel);
int _DisableDmac(int channel);
int _iEnableIntc(int cause);
int _iDisableIntc(int cause);
int _iEnableDmac(int channel);
int _iDisableDmac(int channel);
int iEnableIntc(int cause);
int iDisableIntc(int cause);
int iEnableDmac(int channel);
int iDisableDmac(int channel);

int SetAlarm(unsigned short time, void (*handler)(int id, unsigned short time, void *arg),
             void *arg);

int DIntr(void);
int EIntr(void);

/* Threads.  Six of these names are Windows kernel32 calls too, and the
 * kernel32 import library defines each as a call thunk in an object of its
 * own; libgcc's thread shim (gthr-win32, linked with the C++ runtime the
 * upscaler brings) imports GetThreadId, so the game's GetThreadId and the
 * thunk met in one link (tools/package_win.sh, v0.11.0).  On Windows the C
 * names stay as the game wrote them and the linker symbols are prefixed
 * (port/platform/kernel_host.c defines them through this header); the
 * other platforms are untouched. */
#if defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
#define ICO_PS2_SYM(name) __asm__("ico_ps2_" name)
#else
#define ICO_PS2_SYM(name)
#endif

int CreateThread(struct ThreadParam *param) ICO_PS2_SYM("CreateThread");
int DeleteThread(int id);
int StartThread(int id, void *arg);
void ExitThread(void) ICO_PS2_SYM("ExitThread");
void ExitDeleteThread(void);
int TerminateThread(int id) ICO_PS2_SYM("TerminateThread");
int ChangeThreadPriority(int id, int priority);
int RotateThreadReadyQueue(int priority);
int GetThreadId(void) ICO_PS2_SYM("GetThreadId");
int ReferThreadStatus(int id, struct ThreadParam *info);
int SleepThread(void);
int WakeupThread(int id);
int iWakeupThread(int id);
int CancelWakeupThread(int id);
int SuspendThread(int id) ICO_PS2_SYM("SuspendThread");
int ResumeThread(int id) ICO_PS2_SYM("ResumeThread");
/* Semaphores. */
int CreateSema(struct SemaParam *param);
int DeleteSema(int sema);
int SignalSema(int sema);
int iSignalSema(int sema);
int WaitSema(int sema);
int PollSema(int sema);
int ReferSemaStatus(int sema, struct SemaParam *info);
/* Miscellany. */
void Exit(int status);
void FlushCache(int operation);
void SyncDCache(void *start, void *end);
unsigned long long GsGetIMR(void);
unsigned long long GsPutIMR(unsigned long long imr);
void SetGsCrt(short interlace, short omode, short ffmd);
void SetVSyncFlag(unsigned int *flag, unsigned long long *csr);
void SetVTLBRefillHandler(int cause, void *handler);
void SetVCommonHandler(int cause, void *handler);
void VSync(void);
long long VSync2(void);
void scePrintf(char *fmt, ...);

#endif /* ICO_COMPAT_EEKERNEL_H */
