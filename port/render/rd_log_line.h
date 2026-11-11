/* rd_log_line.h: one log line as one write (rd_core.c rd__log).
 *
 * The log streams are unbuffered on Android (fds 1 and 2 are a pipe the
 * ico-log thread drains, host_android.c) and on Linux and macOS (each
 * write(2) straight to the file), so a line written in three calls (its
 * prefix, its text, its newline) can have another thread's line land
 * between them.  rd_log_line_write formats the prefix, the text, a suffix and
 * the newline into one buffer (on the stack up to RD_LOG_LINE_STACK bytes, else
 * on the heap) and hands it to the stream in one fwrite, which an
 * unbuffered stdio stream passes to the system as one write.  Only where
 * the heap refuses a long line does it fall back to the three calls.
 *
 * Pure: stdio only (rd_log_line_test). */
#ifndef PORT_RENDER_RD_LOG_LINE_H
#define PORT_RENDER_RD_LOG_LINE_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

#define RD_LOG_LINE_STACK 2048u

/* "<prefix><fmt's text><suffix>\n" to f in one fwrite (prefix and suffix
 * may be NULL); the bytes handed to the stream (0 when nothing could be
 * formatted).  ap is used as vfprintf uses it. */
size_t rd_log_line_write(FILE *f, const char *prefix, const char *suffix, const char *fmt,
                         va_list ap);

#endif
