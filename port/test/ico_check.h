/*
 * port/test/ico_check.h
 *
 * The checks a port C test needs, with no dependency beyond the C library:
 *
 *   CHECK(cond)                 on failure, "FAIL file:line: cond" on stderr
 *   CHECK(cond, fmt, ...)       on failure, "FAIL file:line: " and the
 *                               printf-style message instead of cond
 *   CHECK_STR(got, want)        two strings equal (NULL is "(null)")
 *   ico_check_summary(name)     "name: N failure(s)" or "name: ok"; returns
 *                               the exit code, 1 or 0
 *
 * Each failure counts once in ico_check_failures. The Settings tests keep
 * port/ui/test/settings_fixture.h, which has its own CHECK over the game's
 * headers.
 */
#ifndef ICO_PORT_TEST_ICO_CHECK_H
#define ICO_PORT_TEST_ICO_CHECK_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int ico_check_failures;

/* fmt "" (CHECK with no message): the condition's text instead */
static inline void ico_check_fail(const char *file, int line, const char *cond, const char *fmt,
                                  ...)
{
    va_list ap;

    fprintf(stderr, "FAIL %s:%d: ", file, line);
    if (fmt[0] == '\0') {
        fputs(cond, stderr);
    } else {
        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);
    }
    fputc('\n', stderr);
    ico_check_failures++;
}

/* the "" after the arguments is the empty message of a CHECK(cond); after
   a message it is one more argument vfprintf does not read */
#define CHECK(...) ICO_CHECK_(__VA_ARGS__, "")
#define ICO_CHECK_(cond, ...)                                                                      \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ico_check_fail(__FILE__, __LINE__, #cond, __VA_ARGS__);                                \
        }                                                                                          \
    } while (0)

static inline void ico_check_str(const char *file, int line, const char *got, const char *want)
{
    if (got == NULL || want == NULL ? got != want : strcmp(got, want) != 0) {
        ico_check_fail(file, line, "", "\"%s\", want \"%s\"", got ? got : "(null)",
                       want ? want : "(null)");
    }
}

#define CHECK_STR(got, want) ico_check_str(__FILE__, __LINE__, (got), (want))

static inline int ico_check_summary(const char *name)
{
    if (ico_check_failures != 0) {
        fprintf(stderr, "%s: %d failure(s)\n", name, ico_check_failures);
        return 1;
    }
    printf("%s: ok\n", name);
    return 0;
}

#endif /* ICO_PORT_TEST_ICO_CHECK_H */
