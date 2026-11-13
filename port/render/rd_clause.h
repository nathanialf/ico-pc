/* rd_clause.h: one clause appended to a 10 s line at *at (rd_date_one.c,
 * rd_shadow_box.c, rd_no_volume.c): w is snprintf's return for the clause
 * written at out + *at.  It fits whole (at moves past it), or it is dropped
 * whole: the cut fragment replaced by "; ..." where that fits (at moves
 * past the mark, so a later shorter clause still follows it), else by
 * nothing.  A line that fits is unchanged; a cut line never ends inside a
 * clause, and the mark says one was left out. */
#ifndef PORT_RENDER_RD_CLAUSE_H
#define PORT_RENDER_RD_CLAUSE_H

#include <stddef.h>
#include <string.h>

#define RD_CLAUSE_CUT "; ..."

static inline void rd_clause_done(char *out, size_t size, size_t *at, int w)
{
    if (*at >= size) {
        return;
    }
    if (w >= 0 && (size_t)w < size - *at) {
        *at += (size_t)w;
        return;
    }
    if (sizeof RD_CLAUSE_CUT <= size - *at) {
        memcpy(out + *at, RD_CLAUSE_CUT, sizeof RD_CLAUSE_CUT);
        *at += sizeof RD_CLAUSE_CUT - 1u;
    } else {
        out[*at] = '\0';
    }
}

#endif
