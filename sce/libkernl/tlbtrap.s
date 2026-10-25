/* libkernl.a member tlbtrap.o: the TLB refill and debug exception entries,
 * the launcher they eret into and the TLB handler's return syscall, each
 * instruction on its own source line of tlbtrap.s. Every entry is 64-byte
 * aligned: _kTLBException, _xlaunch, _kExitTLBHandler and _kDebugException
 * sit at 0x265040, 0x265140, 0x265180 and 0x265280 (USA 0x24B500, 0x24B600,
 * 0x24B640 and 0x24B740; Aug-6 prototype 0x247A00, 0x247B00, 0x247B40 and
 * 0x247C40; Jun-26 demo 0x234280, 0x234380, 0x2343C0 and 0x2344C0). Of
 * _xlaunch's lines only 92 and 96 are known; the others are placed in order.
 */

	.text
	.set	noreorder
	.set	noat
	.globl	_kTLBException
	.globl	_xlaunch
	.globl	_kExitTLBHandler
	.globl	_kDebugException






	.align	6
_kTLBException:
	lui	$26, %hi(_tlbSaveGpr)
	addiu	$26, $26, %lo(_tlbSaveGpr)

	sq	$1, 16($26)
	sq	$2, 32($26)
	sq	$3, 48($26)
	sq	$4, 64($26)
	sq	$5, 80($26)
	sq	$6, 96($26)
	sq	$7, 112($26)
	sq	$8, 128($26)
	sq	$9, 144($26)
	sq	$10, 160($26)
	sq	$11, 176($26)
	sq	$12, 192($26)
	sq	$13, 208($26)
	sq	$14, 224($26)
	sq	$15, 240($26)
	sq	$16, 256($26)
	sq	$17, 272($26)
	sq	$18, 288($26)
	sq	$19, 304($26)
	sq	$20, 320($26)
	sq	$21, 336($26)
	sq	$22, 352($26)
	sq	$23, 368($26)
	sq	$24, 384($26)
	sq	$25, 400($26)
	sq	$28, 448($26)
	sq	$29, 464($26)
	sq	$30, 480($26)
	sq	$31, 496($26)


	mfhi	$2
	sd	$2, _tlbSaveHi
	mfhi1	$2
	sd	$2, _tlbSaveHi1
	mflo	$2
	sd	$2, _tlbSaveLo
	mflo1	$2
	sd	$2, _tlbSaveLo1
	mfsa	$2
	sd	$2, _tlbSaveSa


	mfc0	$4, $12
	mfc0	$5, $13
	mfc0	$6, $14
	mfc0	$7, $8
	la	$8, _tlbSaveGpr

	sw	$6, _tlbSaveEpc


	lui	$1, %hi(_xlaunch)
	addiu	$1, $1, %lo(_xlaunch)
	mtc0	$1, $14
	sync.p

	mfc0	$1, $12
	li	$2, -2
	and	$1, $1, $2
	mtc0	$1, $12
	sync.p

	eret
_xlaunch:
	lw	$1, _kTLBRefillHandler
	lui	$29, %hi(_tlbSaveGpr)
	jalr	$1

	addiu	$29, $29, %lo(_tlbSaveGpr)
	li	$3, -84
	syscall

	.align	6

_kExitTLBHandler:
	mfc0	$1, $12 ; li $26, -28 ; and $1, $1, $26 ; mtc0 $1, $12 ; sync.p

	lw	$2, _tlbSaveEpc
	mtc0	$2, $14
	sync.p

	ld	$2, _tlbSaveHi
	mthi	$2
	ld	$2, _tlbSaveHi1
	mthi1	$2
	ld	$2, _tlbSaveLo
	mtlo	$2
	ld	$2, _tlbSaveLo1
	mtlo1	$2
	ld	$2, _tlbSaveSa
	mtsa	$2
	sync.p

	lui	$26, %hi(_tlbSaveGpr)
	addiu	$26, $26, %lo(_tlbSaveGpr)

	lq	$1, 16($26)
	lq	$2, 32($26)
	lq	$3, 48($26)
	lq	$4, 64($26)
	lq	$5, 80($26)
	lq	$6, 96($26)
	lq	$7, 112($26)
	lq	$8, 128($26)
	lq	$9, 144($26)
	lq	$10, 160($26)
	lq	$11, 176($26)
	lq	$12, 192($26)
	lq	$13, 208($26)
	lq	$14, 224($26)
	lq	$15, 240($26)
	lq	$16, 256($26)
	lq	$17, 272($26)
	lq	$18, 288($26)
	lq	$19, 304($26)
	lq	$20, 320($26)
	lq	$21, 336($26)
	lq	$22, 352($26)
	lq	$23, 368($26)
	lq	$24, 384($26)
	lq	$25, 400($26)
	lq	$28, 448($26)
	lq	$29, 464($26)
	lq	$30, 480($26)
	lq	$31, 496($26)

	mfc0	$26, $12 ; ori $26, $26, 0x13 ; mtc0 $26, $12 ; sync.p ; eret




	.align	6

_kDebugException:
	lui	$26, %hi(_tlbSaveGpr)
	addiu	$26, $26, %lo(_tlbSaveGpr)

	sq	$1, 16($26)
	sq	$2, 32($26)
	sq	$3, 48($26)
	sq	$4, 64($26)
	sq	$5, 80($26)
	sq	$6, 96($26)
	sq	$7, 112($26)
	sq	$8, 128($26)
	sq	$9, 144($26)
	sq	$10, 160($26)
	sq	$11, 176($26)
	sq	$12, 192($26)
	sq	$13, 208($26)
	sq	$14, 224($26)
	sq	$15, 240($26)
	sq	$16, 256($26)
	sq	$17, 272($26)
	sq	$18, 288($26)
	sq	$19, 304($26)
	sq	$20, 320($26)
	sq	$21, 336($26)
	sq	$22, 352($26)
	sq	$23, 368($26)
	sq	$24, 384($26)
	sq	$25, 400($26)
	sq	$28, 448($26)
	sq	$29, 464($26)
	sq	$30, 480($26)
	sq	$31, 496($26)


	mfhi	$2
	sd	$2, _tlbSaveHi
	mfhi1	$2
	sd	$2, _tlbSaveHi1
	mflo	$2
	sd	$2, _tlbSaveLo
	mflo1	$2
	sd	$2, _tlbSaveLo1
	mfsa	$2
	sd	$2, _tlbSaveSa


	mfc0	$4, $12
	mfc0	$5, $13
	mfc0	$6, $14
	mfc0	$7, $8
	mfc0	$8, $23
	la	$9, _tlbSaveGpr

	lui	$1, %hi(1f)
	addiu	$1, $1, %lo(1f)
	mtc0	$1, $14
	sync.p

	mfc0	$1, $12
	li	$2, -2
	and	$1, $1, $2
	mtc0	$1, $12
	sync.p

	eret
1:
	andi	$2, $5, 0x7c
	lui	$1, %hi(_kDebugHandler)
	addu	$1, $1, $2
	lw	$1, %lo(_kDebugHandler)($1)
	lui	$29, %hi(_tlbSaveGpr)
	jalr	$1
	addiu	$29, $29, %lo(_tlbSaveGpr)

	break	1023, 1023

/* The member's .bss (0x122C, 64-byte aligned): the
   handlers' 0x1000-byte stack, whose top is the register save area the
   entries store the 32 GPRs into, then HI, HI1, LO, LO1, SA and the EPC.
   All local: tlbtrap.o's .bss has no global symbol. */
	.bss
	.align	6
	.space	0x1000
_tlbSaveGpr:	/* derived name */
	.space	32 * 16
_tlbSaveHi:	/* derived name */
	.space	8
_tlbSaveHi1:	/* derived name */
	.space	8
_tlbSaveLo:	/* derived name */
	.space	8
_tlbSaveLo1:	/* derived name */
	.space	8
_tlbSaveSa:	/* derived name */
	.space	8
_tlbSaveEpc:	/* derived name */
	.space	4
