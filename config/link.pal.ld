/* config/link.pal.ld: the link script of the PAL retail ELF (SCES_507.60).
 *
 * Hand-written. Every output section sits at the base ELF's address, so a
 * section that runs long is an ld overlap error (no --no-check-sections). Inputs are taken in the
 * one object order of config/link_order.pal.txt, which is the order the objects
 * reach ld's command line. ld makes the one PT_LOAD itself, at the base's
 * file offset 0x1000 and alignment, the page size of its elf32-littlemips
 * output (a PHDRS command leaves .sbss/.bss out of its memsz).
 *
 * The two INCLUDEs: while the order file carries align.<sec>= tokens (four
 * align.data=16 lines on 2026-10-01), tools/gen_ninja.py lists .data and
 * .rodata input by input there; with none they are empty and the wildcard
 * after them places everything.
 *
 * The linker is GNU ld 2.10 with tools/binutils-2.10-ee.patch (built by
 * tools/setup.sh), writing elf32-littlemips as MAIN.MAP's OUTPUT line does:
 * that era's BFD gives the base's .shstrtab order, NOBITS file offsets and
 * .reginfo entsize 1, the patch the R5900 e_flags and the .DVP.ovlytab link.
 * ld 2.10 has no INCLUDE inside a section, so tools/gen_ninja.py links
 * with build/link.ld, this file with the INCLUDE lines expanded.
 */
ENTRY(_start)

SECTIONS
{
    /* MAIN.MAP line 6795's `_gp=(ALIGN(0x80)+0x7ff0)`, evaluated where the
     * retail link reaches it, the start of .lit4: 0x00638B00 + 0x7FF0. */
    _gp = 0x00640AF0;
    /* The SDK script's start-up values, MAIN.MAP lines 724-726: crt0 hands
     * _stack and _stack_size to the SetupThread syscall and _heap_size to
     * SetupHeap. */
    _stack_size = 0x10000;
    _stack = 0x1ff0000;
    _heap_size = 0x10000;
    /* TRANSITIONAL (finishing pass P1-3, P1-3b): main.c defines pad and boyGObj
     * under their MAIN.MAP names; three consumers still read them through
     * placeholders whose local types conflict with main.h's (each site's
     * kept-local comment states both types). These bind the placeholders to the
     * definitions until the records are typed (phase P4), then they go. */
    D_0028F8F0 = pad;
    D_0028F948 = pad + 0x58;
    D_0028F954 = pad + 0x64;
    D_00639EA4 = boyGObj;

    .text 0x00100000 : { *(.text) }
    .vutext 0x00289BD0 : { *(.vutext) }
    /* Synthesised by ld's MIPS backend from the inputs' .reginfo; not ALLOC,
     * as in the base, so it takes no room in the image. */
    .reginfo 0x0028ECB0 : { *(.reginfo) }
    .data 0x0028ED00 : {
        INCLUDE build/data.inputs.ld
        *(.data .data.*)
    }
    /* The assignment keeps the output section when its inputs are empty,
     * as the base's .vudata and .vubss are. */
    .vudata 0x0054D320 : { *(.vudata) . = .; }
    .rodata 0x0054D380 : {
        INCLUDE build/rodata.inputs.ld
        *(.rodata .rodata.*)
    }
    .lit4 0x00638B00 : { *(.lit4) }
    .sdata 0x00639C80 : { *(.sdata .sdata.*) }
    /* crt0's _start clears memory from _fbss to _end, the SDK crt0's
     * `la $2, _fbss` / `la $3, _end`, and hands _end to SetupHeap. */
    _fbss = 0x0063C100;
    .sbss 0x0063C100 (NOLOAD) : { *(.sbss) *(.scommon) }
    .bss 0x0063C600 (NOLOAD) : { *(.bss) *(COMMON) }
    .vubss . (NOLOAD) : { *(.vubss) . = .; }
    _end = .;
    /* dvp-as's overlay table and its string table, not allocated: the base
     * has both at the end of .vubss. The .DVP.overlay.* sections follow as
     * orphans, one output section per input section at address 0, by the
     * linker's own DVP rule (tools/binutils-2.10-dvp-ld.patch). */
    .DVP.ovlytab 0x00736198 : { *(.DVP.ovlytab) }
    .DVP.ovlystrtab 0x00736198 : { *(.DVP.ovlystrtab) }

    /* Sections the objects carry and the base ELF has none of. */
    /DISCARD/ : { *(.mdebug) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.gnu.attributes) }
}
