/* config/link.kor.ld: the link script of the Korean retail ELF (SCPS_560.01).
 *
 * Hand-written, from config/link.pal.ld on main with every output section at
 * the KOR base ELF's address (its section headers). Every output section sits
 * at the base ELF's address, so a section that comes up short fails the byte
 * gate on its own and one that runs long is an ld overlap error (no
 * --no-check-sections). Inputs are taken in the one object order of
 * config/link_order.kor.txt, which is the order the objects reach ld's command
 * line. ld makes the one PT_LOAD itself, at the base's file offset 0x1000 and
 * alignment, the page size of its elf32-littlemips output (a PHDRS command
 * leaves .sbss/.bss out of its memsz).
 *
 * The two INCLUDEs: while the order file carries align.<sec>= tokens,
 * tools/gen_ninja.py lists .data and .rodata input by input there; with none
 * they are empty and the wildcard after them places everything.
 *
 * The linker is GNU ld 2.10 with tools/binutils-2.10-ee.patch (built by
 * tools/setup.sh), writing elf32-littlemips as the PAL MAIN.MAP's OUTPUT line
 * does; the KOR disc ships no map. ld 2.10 has no INCLUDE inside a section, so
 * tools/gen_ninja.py links with build/kor/link.ld, this file with the INCLUDE
 * lines expanded.
 *
 * PAL's script also binds four TRANSITIONAL placeholders (D_0028F8F0,
 * D_0028F948, D_0028F954, D_00639EA4) to pad and boyGObj. No source on this
 * branch spells them and no object of the PAL build references them, so they
 * are not carried here.
 */
ENTRY(_start)

SECTIONS
{
    /* The PAL MAIN.MAP's `_gp=(ALIGN(0x80)+0x7ff0)`, evaluated at the start of
     * .lit4: 0x00632480 + 0x7FF0. The KOR ELF's .reginfo ri_gp_value is
     * 0x0063A470, the same value. */
    _gp = 0x0063A470;
    /* The SDK script's start-up values, PAL MAIN.MAP lines 724-726: crt0 hands
     * _stack and _stack_size to the SetupThread syscall and _heap_size to
     * SetupHeap. */
    _stack_size = 0x10000;
    _stack = 0x1ff0000;
    _heap_size = 0x10000;

    .text 0x00100000 : { *(.text) }
    .vutext 0x00287B60 : { *(.vutext) }
    /* Synthesised by ld's MIPS backend from the inputs' .reginfo; not ALLOC,
     * as in the base, so it takes no room in the image. */
    .reginfo 0x0028CC40 : { *(.reginfo) }
    .data 0x0028CC80 : {
        INCLUDE build/kor/data.inputs.ld
        *(.data .data.*)
    }
    /* The assignment keeps the output section when its inputs are empty,
     * as the base's .vudata and .vubss are. */
    .vudata 0x00548438 : { *(.vudata) . = .; }
    .rodata 0x00548480 : {
        INCLUDE build/kor/rodata.inputs.ld
        *(.rodata .rodata.*)
    }
    .lit4 0x00632480 : { *(.lit4) }
    .sdata 0x00633580 : { *(.sdata .sdata.*) }
    /* crt0's _start clears memory from _fbss to _end, the SDK crt0's
     * `la $2, _fbss` / `la $3, _end`, and hands _end to SetupHeap. */
    _fbss = 0x00635A00;
    .sbss 0x00635A00 (NOLOAD) : { *(.sbss) *(.scommon) }
    .bss 0x00635F00 (NOLOAD) : { *(.bss) *(COMMON) }
    .vubss . (NOLOAD) : { *(.vubss) . = .; }
    _end = .;
    /* dvp-as's overlay table and its string table, not allocated: the base
     * has both at the end of .vubss. The .DVP.overlay.* sections follow as
     * orphans, one output section per input section at address 0, by the
     * linker's own DVP rule (tools/binutils-2.10-dvp-ld.patch). */
    .DVP.ovlytab 0x0072F898 : { *(.DVP.ovlytab) }
    .DVP.ovlystrtab 0x0072F898 : { *(.DVP.ovlystrtab) }

    /* Sections the objects carry and the base ELF has none of. */
    /DISCARD/ : { *(.mdebug) *(.MIPS.abiflags) *(.pdr) *(.comment) *(.gnu.attributes) }
}
