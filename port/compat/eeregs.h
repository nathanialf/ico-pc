/*
 * port/compat/eeregs.h
 *
 * The host build's eeregs.h: the EE's memory-mapped I/O registers under the
 * names of sce/libkernl/eeregs.h (this project's own clean-room header,
 * MIT). On the host each name points into a plain memory block that stands
 * for the register page (port/platform/hwregs.c): a store lands there and a
 * load reads back the last store, and nothing reacts. The game's register
 * code (timers, DMA kicks, GS_CSR polling) is replaced by the packages that
 * own it; this header only lets it compile. The two blocks are the EE I/O
 * page at 0x10000000 and the GS privileged page at 0x12000000, and each
 * name's offset is its EE address minus the block base.
 *
 * The GS privileged registers are 64 bits wide: `long long` here, where the
 * EE header said `long` (64 bits on the EE, 32 on Windows hosts).
 */
#ifndef ICO_COMPAT_EEREGS_H
#define ICO_COMPAT_EEREGS_H

/* The register pages, 16-byte aligned (port/platform/hwregs.c). */
extern volatile unsigned char ico_hw_eeio[0x10000];
extern volatile unsigned char ico_hw_gs[0x2000];

/* Timers. */
#define T0_COUNT ((volatile int *)(ico_hw_eeio + 0x0000))
#define T0_MODE ((volatile int *)(ico_hw_eeio + 0x0010))
#define T1_COUNT ((volatile int *)(ico_hw_eeio + 0x0800))
#define T1_MODE ((volatile int *)(ico_hw_eeio + 0x0810))
#define T3_MODE ((volatile int *)(ico_hw_eeio + 0x1810))
/* IPU. */
#define IPU_CMD ((volatile int *)(ico_hw_eeio + 0x2000))
#define IPU_CTRL ((volatile int *)(ico_hw_eeio + 0x2010))
#define IPU_BP ((volatile int *)(ico_hw_eeio + 0x2020))
#define IPU_TOP ((volatile int *)(ico_hw_eeio + 0x2030))
#define IPU_in_FIFO ((volatile void *)(ico_hw_eeio + 0x7010))
/* GIF. */
#define GIF_CTRL ((volatile int *)(ico_hw_eeio + 0x3000))
#define GIF_STAT ((volatile int *)(ico_hw_eeio + 0x3020))
/* VIF0. */
#define VIF0_FBRST ((volatile int *)(ico_hw_eeio + 0x3810))
#define VIF0_ERR ((volatile int *)(ico_hw_eeio + 0x3820))
#define VIF0_MARK ((volatile int *)(ico_hw_eeio + 0x3830))
#define VIF0_FIFO ((volatile void *)(ico_hw_eeio + 0x4000))
/* VIF1. */
#define VIF1_STAT ((volatile int *)(ico_hw_eeio + 0x3C00))
#define VIF1_FBRST ((volatile int *)(ico_hw_eeio + 0x3C10))
#define VIF1_FIFO ((volatile void *)(ico_hw_eeio + 0x5000))
/* DMA channel 0, VIF0. */
#define D0_CHCR ((volatile int *)(ico_hw_eeio + 0x8000))
/* DMA channel 1, VIF1. */
#define D1_CHCR ((volatile int *)(ico_hw_eeio + 0x9000))
#define D1_MADR ((volatile int *)(ico_hw_eeio + 0x9010))
#define D1_QWC ((volatile int *)(ico_hw_eeio + 0x9020))
#define D1_TADR ((volatile int *)(ico_hw_eeio + 0x9030))
/* DMA channel 2, GIF. */
#define D2_CHCR ((volatile int *)(ico_hw_eeio + 0xA000))
#define D2_MADR ((volatile int *)(ico_hw_eeio + 0xA010))
#define D2_QWC ((volatile int *)(ico_hw_eeio + 0xA020))
#define D2_TADR ((volatile int *)(ico_hw_eeio + 0xA030))
/* DMA channel 3, from IPU. */
#define D3_CHCR ((volatile int *)(ico_hw_eeio + 0xB000))
#define D3_MADR ((volatile int *)(ico_hw_eeio + 0xB010))
#define D3_QWC ((volatile int *)(ico_hw_eeio + 0xB020))
/* DMA channel 4, to IPU. */
#define D4_CHCR ((volatile int *)(ico_hw_eeio + 0xB400))
#define D4_MADR ((volatile int *)(ico_hw_eeio + 0xB410))
#define D4_QWC ((volatile int *)(ico_hw_eeio + 0xB420))
#define D4_TADR ((volatile int *)(ico_hw_eeio + 0xB430))
/* DMA channels 5 to 7, SIF0 to SIF2. */
#define D5_CHCR ((volatile int *)(ico_hw_eeio + 0xC000))
#define D6_CHCR ((volatile int *)(ico_hw_eeio + 0xC400))
#define D7_CHCR ((volatile int *)(ico_hw_eeio + 0xC800))
/* DMA channel 8, from scratchpad. */
#define D8_CHCR ((volatile int *)(ico_hw_eeio + 0xD000))
#define D8_MADR ((volatile int *)(ico_hw_eeio + 0xD010))
#define D8_QWC ((volatile int *)(ico_hw_eeio + 0xD020))
#define D8_SADR ((volatile int *)(ico_hw_eeio + 0xD080))
/* DMA channel 9, to scratchpad. */
#define D9_CHCR ((volatile int *)(ico_hw_eeio + 0xD400))
#define D9_MADR ((volatile int *)(ico_hw_eeio + 0xD410))
#define D9_QWC ((volatile int *)(ico_hw_eeio + 0xD420))
#define D9_TADR ((volatile int *)(ico_hw_eeio + 0xD430))
#define D9_SADR ((volatile int *)(ico_hw_eeio + 0xD480))
/* DMA controller. */
#define D_CTRL ((volatile int *)(ico_hw_eeio + 0xE000))
#define D_STAT ((volatile int *)(ico_hw_eeio + 0xE010))
#define D_PCR ((volatile int *)(ico_hw_eeio + 0xE020))
#define D_SQWC ((volatile int *)(ico_hw_eeio + 0xE030))
#define D_RBSR ((volatile int *)(ico_hw_eeio + 0xE040))
#define D_RBOR ((volatile int *)(ico_hw_eeio + 0xE050))
#define D_STADR ((volatile int *)(ico_hw_eeio + 0xE060))
#define D_ENABLER ((volatile int *)(ico_hw_eeio + 0xF520))
#define D_ENABLEW ((volatile int *)(ico_hw_eeio + 0xF590))
/* Interrupt controller. */
#define INTC_STAT ((volatile int *)(ico_hw_eeio + 0xF000))
/* GS privileged registers, 64 bits wide. */
#define GS_PMODE ((volatile long long *)(ico_hw_gs + 0x0000))
#define GS_SMODE2 ((volatile long long *)(ico_hw_gs + 0x0020))
#define GS_DISPFB1 ((volatile long long *)(ico_hw_gs + 0x0070))
#define GS_DISPLAY1 ((volatile long long *)(ico_hw_gs + 0x0080))
#define GS_DISPFB2 ((volatile long long *)(ico_hw_gs + 0x0090))
#define GS_DISPLAY2 ((volatile long long *)(ico_hw_gs + 0x00A0))
#define GS_EXTDATA ((volatile long long *)(ico_hw_gs + 0x00C0))
#define GS_BGCOLOR ((volatile long long *)(ico_hw_gs + 0x00E0))
#define GS_CSR ((volatile unsigned long long *)(ico_hw_gs + 0x1000))
#define GS_BUSDIR ((volatile unsigned long long *)(ico_hw_gs + 0x1040))
#endif /* ICO_COMPAT_EEREGS_H */
