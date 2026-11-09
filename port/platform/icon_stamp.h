/*
 * port/platform/icon_stamp.h
 *
 * The exe's icon (docs/port/APP_ICON.md, "The exe's icon"): the pieces of
 * tools/ico_icon_stamp.c that need no Windows call, so a test checks them
 * on every platform.
 *
 *   ico_icon_stamp_build   an .ico file (the capture's, app_icon_ico.c) into
 *                          the resources an exe carries an icon as: one
 *                          RT_ICON per image (ids 1..n, the image's bytes as
 *                          they are: a BMP entry's BITMAPINFOHEADER and a
 *                          PNG entry's file are both legal payloads) and the
 *                          RT_GROUP_ICON directory (GRPICONDIR: the ICONDIR
 *                          with each entry's 4-byte file offset replaced by
 *                          the image's 2-byte resource id)
 *   ico_icon_stamp_text    the stamp the exe carries as RT_RCDATA
 *                          "ICO_ICON_STAMP": the icon's .txt (the package
 *                          label, "dev" in a development build) and the disc
 *   ico_icon_stamp_read    the stamp resource's bytes back into a string
 *   ico_icon_stamp_quote   one argument of a Windows command line, quoted as
 *                          CommandLineToArgvW and the C runtime split it
 *   ico_icon_stamp_pe_check  the stamped copy of the exe against the exe:
 *                          what the loader runs is unchanged (the check
 *                          before the copy takes the exe's place)
 *   ico_icon_stamp_clear_symbols  the stamped copy's dangling COFF symbol
 *                          table pointer zeroed
 *   ico_icon_stamp_refusal / _refused  the helper's log line for a stamp
 *                          it refuses for good, and the game's test of it
 *
 * Plain C, no allocation: the resources point into the caller's .ico bytes.
 */
#ifndef ICO_PLATFORM_ICON_STAMP_H
#define ICO_PLATFORM_ICON_STAMP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* an exe's icon group holds at most this many images here (the capture
   writes 4) */
#define ICO_ICON_STAMP_MAX 32
/* GRPICONDIR: 6 bytes, then 14 a GRPICONDIRENTRY */
#define ICO_ICON_STAMP_GROUP_MAX (6 + 14 * ICO_ICON_STAMP_MAX)
/* the resource name of the stamp (RT_RCDATA) */
#define ICO_ICON_STAMP_NAME "ICO_ICON_STAMP"

typedef struct IcoIconStampImage {
    const uint8_t *data; /* into the .ico bytes */
    uint32_t size;
    uint16_t id;    /* the RT_ICON id, 1..count */
    uint16_t width; /* 1..256 (the entry's byte, 0 read as 256) */
    uint16_t height;
    uint16_t bitCount;
    int png; /* 1 when the image is a PNG file */
} IcoIconStampImage;

typedef struct IcoIconStampResources {
    int count;
    IcoIconStampImage image[ICO_ICON_STAMP_MAX];
    uint8_t group[ICO_ICON_STAMP_GROUP_MAX]; /* the RT_GROUP_ICON bytes */
    size_t groupSize;                        /* 6 + 14 * count */
} IcoIconStampResources;

/* ico[0..n) into *out.  0, or -1 with *why (when why is not NULL) naming
   what is wrong: not an icon file (reserved 0, type 1), no images or more
   than ICO_ICON_STAMP_MAX, the directory or an image past the file's end,
   an empty image. */
int ico_icon_stamp_build(const uint8_t *ico, size_t n, IcoIconStampResources *out,
                         const char **why);

/* "<label> <disc>" into out (label "" or NULL is written "dev").  0, or -1
   when it does not fit or disc is missing (out ""). */
int ico_icon_stamp_text(char *out, size_t size, const char *label, const char *disc);

/* The stamp resource's bytes (no terminator of their own) into out, up to
   the first line end or NUL.  0, or -1 when data is NULL, n is 0, or the
   text does not fit (out "").  The exe's stamp equals the wanted one when
   this and ico_icon_stamp_text agree byte for byte. */
int ico_icon_stamp_read(char *out, size_t size, const uint8_t *data, size_t n);

/* arg appended to out[0..*used) as one command-line argument: in double
   quotes, a quote as \", backslashes doubled only before a quote or the
   closing quote; a space before it when *used is not 0.  0, or -1 when it
   does not fit (out unchanged past *used). */
int ico_icon_stamp_quote(char *out, size_t size, size_t *used, const char *arg);

/* The stamped copy b[0..bn) of the program a[0..an) (both PE files): 0 when
   what runs is as it was, else -1 with *why.  EndUpdateResourceW rewrites
   the resource section and moves the sections after it (in the game's exe
   .reloc and the DWARF sections the linker places after .rsrc), so the
   rule is: the same machine, image base and entry point; every section of
   a that starts below a's resource section is in b at the same index with
   the same name, addresses, sizes and flags and the same bytes; the import,
   exception, TLS and import address table directories the same; b has a
   resource directory, every section of b lies inside the file and inside
   its SizeOfImage, b's sections are in address order without overlap, b's
   relocation directory lies inside one of its sections, and b's COFF
   symbol table pointer is 0 or past the end of its sections' bytes (with
   the symbols inside the file). */
int ico_icon_stamp_pe_check(const uint8_t *a, size_t an, const uint8_t *b, size_t bn,
                            const char **why);

/* head[0..n) the start of a PE file (its headers): the COFF header's
   PointerToSymbolTable and NumberOfSymbols set to 0 (the stamped copy:
   EndUpdateResourceW drops the symbol table after the sections but keeps
   the pointer).  0, or -1 when head is not a PE file's start. */
int ico_icon_stamp_clear_symbols(uint8_t *head, size_t n);

/* The helper's log text when it refuses a stamp for good (the icon or the
   stamped copy unusable): "refused stamp \"<stamp>\":" into out.  0, or
   -1 when stamp is empty or it does not fit (out ""). */
int ico_icon_stamp_refusal(char *out, size_t size, const char *stamp);

/* 1 when line (a line of logs\icon-stamp.log) is the helper's refusal of
   stamp (the text after "ico_icon_stamp: " starts with
   ico_icon_stamp_refusal's), else 0: the game then does not start the
   helper again for that stamp; a new stamp is tried. */
int ico_icon_stamp_refused(const char *line, const char *stamp);

#ifdef __cplusplus
}
#endif
#endif /* ICO_PLATFORM_ICON_STAMP_H */
