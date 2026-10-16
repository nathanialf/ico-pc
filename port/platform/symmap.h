/*
 * port/platform/symmap.h
 *
 * The crash and watchdog reports' symbolizer: the program's offsets
 * (ico_pc_x64.exe+0x1b35e4) turned into function names (_CopyVector+0x4)
 * on the player's machine, at the time of the report, from the symbol file
 * shipped with the program:
 *
 *   - a GNU ld link map (-Map: ico_pc_x64.map beside the Windows program,
 *     ico_pc.map beside the Linux one): its symbol lines
 *     ("                0x0000000140197fc0                ico_diag_init")
 *     inside the code's output sections (.text, .init, .fini, .plt), and its
 *     input section lines (" .text  0x...  0x190 libico_platform.a(hotkeys.c.obj)"),
 *     which name the object file an address is in (the map lists global
 *     functions only: a static function shows as the object and the offset
 *     into its code);
 *   - an nm table ("0000000000123456 T name", nm -n of the unstripped
 *     libmain.so: tools/package_android.sh writes it as libmain.syms.gz
 *     into the APK's assets), whose t symbols name static functions too;
 *     its gzip form is read through ico_sym_gz_source.
 *
 * The file's addresses are placed against the running program by an
 * anchor: a function whose address the caller knows (ico_diag_init): the
 * file's address of the anchor minus its offset in the running module is
 * the file's address of the module's start. So the same code reads a PE
 * map (image base 0x140000000), a PIE's (0) and a shared library's table.
 *
 * Bounded and allocation-free, so it runs inside a signal handler: the file
 * is streamed twice (the anchor, then the offsets) through static buffers,
 * one line at a time (lines past ICO_SYM_LINE bytes are cut), every offset
 * compared with every line (at most ICO_SYM_MAX offsets). One resolve at a
 * time (the reports run one at a time). A file that does not parse names
 * nothing; it never stops the report.
 */
#ifndef ICO_PLATFORM_SYMMAP_H
#define ICO_PLATFORM_SYMMAP_H

#include <stddef.h>
#include <stdint.h>

#define ICO_SYM_MAX 64
#define ICO_SYM_LINE 4096
#define ICO_SYM_NAME 96
#define ICO_SYM_OBJ 64

typedef struct IcoSymHit {
    uint64_t off;            /* in: the offset from the module's start */
    char name[ICO_SYM_NAME]; /* out: the function at or before it ("" none) */
    uint64_t delta;          /* out: off minus the function's start */
    char obj[ICO_SYM_OBJ];   /* out: the object file holding it (a map; "" none) */
    uint64_t obj_delta;      /* out: off minus that object's code start */
    int named_by_section;    /* out: name came from a .text.<name> section */
} IcoSymHit;

/* The file, as a stream: read fills buf with up to size bytes (0 at the
   end, < 0 on an error); rewind starts it again (0, or -1). */
typedef struct IcoSymSource {
    long (*read)(void *user, char *buf, size_t size);
    int (*rewind)(void *user);
    void *user;
} IcoSymSource;

/* Names each hits[i].off (n at most ICO_SYM_MAX) from the file src reads,
   placed by the anchor: the function `anchor` (a leading '_' on either side
   ignored) lies anchor_off bytes from the module's start. The number of
   hits named (a function or an object file), or -1 when the anchor is not
   in the file or the file cannot be read. */
int ico_sym_resolve(const IcoSymSource *src, const char *anchor, uint64_t anchor_off,
                    IcoSymHit *hits, int n);

/* "_CopyVector+0x4 (vu0.c.obj)", "a static function in rd_blur.c.o, +0x1c0
   into its code", or "" for a hit with nothing named. */
void ico_sym_describe(const IcoSymHit *h, char *out, size_t size);

/* A gzip stream over raw (the member's deflate data inflated with miniz's
   tinfl into static buffers; one at a time): *out reads the text. */
void ico_sym_gz_source(IcoSymSource *out, const IcoSymSource *raw);

/* A source over memory (tests, and a table already in memory). */
typedef struct IcoSymMem {
    const char *data;
    size_t size, pos;
} IcoSymMem;

void ico_sym_mem_source(IcoSymSource *out, IcoSymMem *m, const void *data, size_t size);

#endif /* ICO_PLATFORM_SYMMAP_H */
