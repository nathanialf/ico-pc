/* ico_ffx_compat.h: forced into every FidelityFX SDK source of
 * ico_ffx_upscaler (port/rhi/ffx/CMakeLists.txt), ahead of volk.h.
 *
 * The SDK (v1.1.4) is written against MSVC: it calls the C library's
 * bounds-checked names (wcscpy_s, strcpy_s, sprintf_s) and _countof, and
 * leans on <Windows.h> having brought in the C and C++ headers it uses
 * (memset, malloc, wstring_convert, floor, placement new). Outside Windows
 * this header supplies them, with the MSVC behaviour the SDK relies on:
 * the copy truncates to the destination and always terminates it. On
 * Windows (mingw) the C library has the names, so only the headers are
 * included. The SDK's own files are never edited by hand: what a header
 * cannot fix is in tools/ffx/patches/ (docs/BUILDING.md). */
#pragma once
#ifdef __cplusplus
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <new>
#include <locale>
#include <mutex>
#include <codecvt>
#include <string>
#ifndef _WIN32
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
inline int wcscpy_s(wchar_t *dst, size_t n, const wchar_t *src)
{
    if (!dst || n == 0)
        return 22;
    size_t len = src ? wcslen(src) : 0;
    if (len >= n)
        len = n - 1;
    if (len)
        wmemcpy(dst, src, len);
    dst[len] = L'\0';
    return 0;
}

template <size_t N> inline int wcscpy_s(wchar_t (&dst)[N], const wchar_t *src)
{
    return wcscpy_s(dst, N, src);
}

inline int strcpy_s(char *dst, size_t n, const char *src)
{
    if (!dst || n == 0)
        return 22;
    size_t len = src ? strlen(src) : 0;
    if (len >= n)
        len = n - 1;
    if (len)
        memcpy(dst, src, len);
    dst[len] = '\0';
    return 0;
}

template <size_t N> inline int strcpy_s(char (&dst)[N], const char *src)
{
    return strcpy_s(dst, N, src);
}

inline int sprintf_s(char *dst, size_t n, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(dst, n, fmt, ap);
    va_end(ap);
    return r;
}
#endif
#endif
