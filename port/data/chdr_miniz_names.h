/*
 * port/data/chdr_miniz_names.h
 *
 * Forced into every file of the ico_libchdr library (port/data/
 * CMakeLists.txt). libchdr bundles its own miniz (3.1.1) and the port links
 * another (port/third_party/miniz, 3.1.2, ico_miniz); both define the same
 * function names, so one program cannot hold both as they are. These
 * defines give libchdr's copy a chdr_ prefix in its definitions and in
 * libchdr's calls alike (miniz.h's zlib-style names, inflate and the rest,
 * expand to these). The list is every external function the copy defines
 * when built inflate-only as upstream builds it (MINIZ_NO_ARCHIVE_APIS,
 * MINIZ_NO_DEFLATE_APIS, MINIZ_NO_STDIO, MINIZ_NO_TIME), taken from the
 * compiled object's symbol table (nm); a name left out would either fail
 * the link as a duplicate or quietly bind one copy's callers to the other.
 */
#ifndef ICO_CHDR_MINIZ_NAMES_H
#define ICO_CHDR_MINIZ_NAMES_H

#define miniz_def_alloc_func chdr_miniz_def_alloc_func
#define miniz_def_free_func chdr_miniz_def_free_func
#define miniz_def_realloc_func chdr_miniz_def_realloc_func
#define mz_adler32 chdr_mz_adler32
#define mz_crc32 chdr_mz_crc32
#define mz_error chdr_mz_error
#define mz_free chdr_mz_free
#define mz_inflate chdr_mz_inflate
#define mz_inflateEnd chdr_mz_inflateEnd
#define mz_inflateInit chdr_mz_inflateInit
#define mz_inflateInit2 chdr_mz_inflateInit2
#define mz_inflateReset chdr_mz_inflateReset
#define mz_uncompress chdr_mz_uncompress
#define mz_uncompress2 chdr_mz_uncompress2
#define mz_version chdr_mz_version
#define tinfl_decompress chdr_tinfl_decompress
#define tinfl_decompress_mem_to_callback chdr_tinfl_decompress_mem_to_callback
#define tinfl_decompress_mem_to_heap chdr_tinfl_decompress_mem_to_heap
#define tinfl_decompress_mem_to_mem chdr_tinfl_decompress_mem_to_mem
#define tinfl_decompressor_alloc chdr_tinfl_decompressor_alloc
#define tinfl_decompressor_free chdr_tinfl_decompressor_free

#endif /* ICO_CHDR_MINIZ_NAMES_H */
