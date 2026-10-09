/*
 * port/platform/fpenv.h
 *
 * The floating-point environment the simulation runs in.
 *
 * The EE's FPU rounds every single-precision result toward zero and flushes
 * denormal inputs and outputs to zero. ico_fpenv_sim_enter() puts the host
 * FPU in the closest IEEE mode: round toward zero with flush-to-zero and
 * denormals-are-zero (MXCSR on x86-64, FPCR on arm64). It does not
 * give the EE's other differences (no infinities or NaNs, clamped results,
 * its own division and square root); those are port/math's helpers.
 *
 * ico_fpenv_host_enter() puts back the host defaults (round to nearest,
 * denormals kept, every exception masked) for host code: the platform layer,
 * SDL, the renderer and the host libc.
 *
 * ICO_FPTRAP (the fptrap preset) also unmasks the divide-by-zero and invalid
 * exceptions in simulation mode, so the first such operation stops the
 * program (SIGFPE on Linux, a structured exception on Windows) at the
 * instruction that raised it.
 */
#ifndef ICO_PLATFORM_FPENV_H
#define ICO_PLATFORM_FPENV_H

void ico_fpenv_sim_enter(void);
void ico_fpenv_host_enter(void);
/* The raw control register (MXCSR or FPCR) as a 64-bit value, for tests and
   trace headers. */
unsigned long long ico_fpenv_raw(void);

#endif /* ICO_PLATFORM_FPENV_H */
