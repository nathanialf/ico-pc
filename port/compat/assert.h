/*
 * port/compat/assert.h
 *
 * The game's assert.h is newlib's (sce/libc/assert.h): assert() calls
 * __assert(file, line, expression), and the reconstructed sources also call
 * __assert directly. The host libc's assert.h has another macro and, on
 * glibc, an __assert with the arguments in another order, so this header
 * stands in for it on the game's include path and names newlib's helper
 * ico_assert (port/platform/assert_host.c), keeping it apart from any
 * host symbol.
 */
#ifndef ICO_COMPAT_ASSERT_H
#define ICO_COMPAT_ASSERT_H

void ico_assert(const char *file, int line, const char *failedexpr);

#define __assert ico_assert
#endif /* ICO_COMPAT_ASSERT_H */
/* Like the C library's, the macro is redefined at every inclusion so NDEBUG
   can change between them. */
#undef assert
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : __assert(__FILE__, __LINE__, #e))
#endif
