/* rd_log_line_test.c: one log line as one write (rd_log_line.h, rd_core.c
 * rd__log and rd__log_once; the open item of the v0.11.5 diagnostics: the
 * three writes a line let another thread's line land inside it).
 *
 *   1. a short line: "rd: " + its text + "\n", whole, in one write;
 *   2. a line with a suffix (rd__log_once's " (reported once)"): whole;
 *   3. a line of 10000 characters (past the stack's buffer, formatted on
 *      the heap): whole, in one write;
 *   4. a line exactly at the stack buffer's edge and one past it: whole;
 *   5. no prefix and no suffix: the text and the newline.
 * The writes are counted where the C library lets a stream's write
 * function be the test's (glibc's fopencookie, the stream unbuffered as the
 * game's are); elsewhere the bytes alone are checked, through a temporary
 * file. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__GLIBC__)
#include <sys/types.h>
#endif

#include "rd_log_line.h"

static int failures;

#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* the stream's bytes and its writes */
static char s_got[16384];
static size_t s_gotN;
static unsigned s_writes;

#if defined(__GLIBC__)
static ssize_t cookieWrite(void *c, const char *buf, size_t size)
{
    (void)c;
    s_writes++;
    if (s_gotN + size <= sizeof(s_got)) {
        memcpy(s_got + s_gotN, buf, size);
        s_gotN += size;
    }
    return (ssize_t)size;
}
#endif

static size_t say(FILE *f, const char *prefix, const char *suffix, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const size_t n = rd_log_line_write(f, prefix, suffix, fmt, ap);
    va_end(ap);
    return n;
}

/* one line through a fresh stream: its bytes into s_got, its writes into
   s_writes (-1 where they cannot be counted) */
static int lineOf(const char *prefix, const char *suffix, const char *text)
{
    s_gotN = 0;
    s_writes = 0;
#if defined(__GLIBC__)
    cookie_io_functions_t io = {NULL, cookieWrite, NULL, NULL};
    FILE *f = fopencookie(NULL, "w", io);
    if (f == NULL) {
        return -2;
    }
    setvbuf(f, NULL, _IONBF, 0); /* as the game's stderr */
    (void)say(f, prefix, suffix, "%s", text);
    fclose(f);
    return (int)s_writes;
#else
    FILE *f = tmpfile();
    if (f == NULL) {
        return -2;
    }
    setvbuf(f, NULL, _IONBF, 0);
    (void)say(f, prefix, suffix, "%s", text);
    rewind(f);
    s_gotN = fread(s_got, 1, sizeof(s_got), f);
    fclose(f);
    return -1;
#endif
}

static void expect(int n, const char *prefix, const char *suffix, const char *text,
                   const char *what)
{
    char want[16384];
    const int w =
        snprintf(want, sizeof(want), "%s%s%s\n", prefix ? prefix : "", text, suffix ? suffix : "");
    CHECK(n != -2, "%s: no stream", what);
    CHECK(w > 0 && (size_t)w == s_gotN && memcmp(want, s_got, s_gotN) == 0,
          "%s: %zu bytes written, %d wanted", what, s_gotN, w);
    CHECK(n < 0 || n == 1, "%s: %d writes, 1 wanted", what, n);
    printf("  %s: %zu bytes, %d writes\n", what, s_gotN, n);
}

int main(void)
{
    static char big[10001], edge[RD_LOG_LINE_STACK + 8];
    /* 1. */
    expect(lineOf("rd: ", NULL, "pipelines: the start-up set is over"), "rd: ", NULL,
           "pipelines: the start-up set is over", "1. a short line");
    /* 2. */
    expect(lineOf("rd: ", " (reported once)", "a stub command"), "rd: ", " (reported once)",
           "a stub command", "2. with a suffix");
    /* 3. */
    for (size_t i = 0; i + 1 < sizeof(big); i++) {
        big[i] = (char)('a' + (char)(i % 26u));
    }
    big[sizeof(big) - 1] = '\0';
    expect(lineOf("rd: ", NULL, big), "rd: ", NULL, big, "3. 10000 characters");
    /* 4. the line (prefix, text, newline) of exactly the stack's size, and
       one byte more */
    const size_t at = RD_LOG_LINE_STACK - 4u - 2u;
    memset(edge, 'x', at);
    edge[at] = '\0';
    expect(lineOf("rd: ", NULL, edge), "rd: ", NULL, edge, "4. at the stack's edge");
    memset(edge, 'y', at + 1u);
    edge[at + 1u] = '\0';
    expect(lineOf("rd: ", NULL, edge), "rd: ", NULL, edge, "4. one past it");
    /* 5. */
    expect(lineOf(NULL, NULL, "plain"), NULL, NULL, "plain", "5. no prefix");
    if (failures) {
        printf("rd_log_line_test: %d failures\n", failures);
        return 1;
    }
    printf("rd_log_line_test: ok\n");
    return 0;
}
