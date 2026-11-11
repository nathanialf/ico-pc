/* rd_log_line.c: one log line as one write (rd_log_line.h). */
#include "rd_log_line.h"

#include <stdlib.h>
#include <string.h>

size_t rd_log_line_write(FILE *f, const char *prefix, const char *suffix, const char *fmt,
                         va_list ap)
{
    char stackBuf[RD_LOG_LINE_STACK];
    const size_t pre = prefix != NULL ? strlen(prefix) : 0u;
    const size_t suf = suffix != NULL ? strlen(suffix) : 0u;
    va_list again;
    va_copy(again, ap);
    if (pre + suf + 2u > sizeof(stackBuf)) {
        va_end(again);
        return 0;
    }
    if (pre != 0u) {
        memcpy(stackBuf, prefix, pre);
    }
    const int n = vsnprintf(stackBuf + pre, sizeof(stackBuf) - pre - suf - 1u, fmt, ap);
    if (n < 0) {
        va_end(again);
        return 0;
    }
    const size_t text = pre + (size_t)n;
    const size_t len = text + suf; /* the line without its newline */
    char *buf = stackBuf;
    if (len + 2u > sizeof(stackBuf)) {
        /* longer than the stack's buffer: formatted again on the heap */
        buf = malloc(len + 2u);
        if (buf == NULL) {
            /* no room: the three writes, as before */
            fputs(prefix != NULL ? prefix : "", f);
            vfprintf(f, fmt, again);
            fputs(suffix != NULL ? suffix : "", f);
            fputc('\n', f);
            va_end(again);
            return len + 1u;
        }
        if (pre != 0u) {
            memcpy(buf, prefix, pre);
        }
        (void)vsnprintf(buf + pre, (size_t)n + 1u, fmt, again);
    }
    va_end(again);
    if (suf != 0u) {
        memcpy(buf + text, suffix, suf);
    }
    buf[len] = '\n';
    const size_t wrote = fwrite(buf, 1, len + 1u, f);
    if (buf != stackBuf) {
        free(buf);
    }
    return wrote;
}
