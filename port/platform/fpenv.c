/*
 * port/platform/fpenv.c
 *
 * The simulation's floating-point mode and the host's (fpenv.h).
 */
#include "fpenv.h"

#if defined(__x86_64__) || defined(__i386__)

#include <xmmintrin.h>

/* MXCSR fields (Intel SDM vol. 1, 10.2.3). */
#define MXCSR_DAZ 0x0040u     /* denormal inputs read as zero */
#define MXCSR_IM 0x0080u      /* invalid operation masked */
#define MXCSR_ZM 0x0200u      /* divide-by-zero masked */
#define MXCSR_RC_ZERO 0x6000u /* rounding control 11: toward zero */
#define MXCSR_FTZ 0x8000u     /* denormal results flushed to zero */
#define MXCSR_DEFAULT 0x1F80u /* power-on value: all masked, nearest, flags clear */

void ico_fpenv_sim_enter(void)
{
    unsigned int csr = MXCSR_DEFAULT | MXCSR_RC_ZERO | MXCSR_FTZ | MXCSR_DAZ;
#ifdef ICO_FPTRAP
    csr &= ~(MXCSR_IM | MXCSR_ZM);
#endif
    _mm_setcsr(csr);
}

void ico_fpenv_host_enter(void)
{
    /* The default has the sticky flags clear, so nothing the simulation
       raised carries over into host code. */
    _mm_setcsr(MXCSR_DEFAULT);
}

unsigned long long ico_fpenv_raw(void)
{
    return _mm_getcsr();
}

#elif defined(__aarch64__)
/* FPCR fields (Arm ARM, C5.2.8). */
#define FPCR_IOE (1ull << 8) /* invalid operation trap enable */
#define FPCR_DZE (1ull << 9) /* divide-by-zero trap enable */
#define FPCR_RZ (3ull << 22) /* RMode 11: toward zero */
#define FPCR_FZ (1ull << 24) /* flush denormals to zero */
#define FPCR_MODE_MASK (FPCR_IOE | FPCR_DZE | FPCR_RZ | FPCR_FZ)

static unsigned long long fpcr_get(void)
{
    unsigned long long v;
    __asm__ __volatile__("mrs %0, fpcr" : "=r"(v));
    return v;
}

static void fpcr_set(unsigned long long v)
{
    __asm__ __volatile__("msr fpcr, %0" : : "r"(v));
}

void ico_fpenv_sim_enter(void)
{
    unsigned long long v = (fpcr_get() & ~FPCR_MODE_MASK) | FPCR_RZ | FPCR_FZ;
#ifdef ICO_FPTRAP
    v |= FPCR_IOE | FPCR_DZE;
#endif
    fpcr_set(v);
}

void ico_fpenv_host_enter(void)
{
    fpcr_set(fpcr_get() & ~FPCR_MODE_MASK);
}

unsigned long long ico_fpenv_raw(void)
{
    return fpcr_get();
}

#else
#error "port/platform/fpenv.c: no floating-point mode code for this architecture"
#endif
