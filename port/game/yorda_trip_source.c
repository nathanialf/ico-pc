/*
 * port/game/yorda_trip_source.c
 *
 * Yorda trips: her fall's motions by name in the disc's motion table, and
 * their files read from the pack that holds them (yorda_trip.h).  Built
 * with the game's include path and record layout (MotionDef); it reads the
 * motion table and the disc, and writes neither.
 *
 * On every disc measured (the European, Japanese, North American and the
 * August 2001 discs) the one pack holding them is STG4A2ND.DF, the main
 * gate's second stage (st04a (GATE_2ND)); the main gate's first stage
 * (STGST04A.DF) holds none.  The pack is found by its members, not by a
 * stage number or a file name.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typedef.h"
#include "motionOrientManager.h"

#include "df_pack.h"
#include "tables.h"
#include "yorda_trip.h"

/* port/data/gen/table_defs.c's writable definitions (the table loader fills
   them from the disc's ELF); motionOrientManager.h declares neither */
extern MotionDef motionKind[];
extern MotionOrientEntry motionOrient[];

/* a motion's memory area: per-stage data, cleared on every stage change
   (motionFileManager.c ResetDynamicMotionManager) */
#define YT_AREA_STAGE 4

static int findName(const char *name, int rows)
{
    for (int i = 0; i < rows; i++) {
        if (strncmp(motionKind[i].name, name, sizeof motionKind[i].name) == 0) {
            return i;
        }
    }
    return -1;
}

int ico_yorda_trip_find_ids(IcoYtIds *out, int rows)
{
    memset(out, 0, sizeof(*out));
    for (int i = 0; i < rows; i++) {
        const char *nm = motionKind[i].name;

        if (motionKind[i].area == YT_AREA_STAGE && strncmp(nm, "GIRL ", 5) == 0 &&
            memchr(nm, '\0', sizeof motionKind[i].name) != NULL && strstr(nm, "HETARI") != NULL &&
            out->n < ICO_YT_MAX_MOTIONS) {
            out->id[out->n++] = i;
        }
    }
    out->run_loop = findName("GIRL HAND_RUNLOOP", rows);
    out->sit = findName("GIRL SIT(HETARI)", rows);
    out->sit_loop = findName("GIRL SITLOOP(HETARI)", rows);
    out->sit_end = findName("GIRL SITEND(HETARI)", rows);
    if (out->run_loop < 0 || out->sit < 0 || out->sit_loop < 0 || out->sit_end < 0) {
        return -1;
    }
    /* where the fall goes on to (motionOrientManager.c GetMotionOrient
       reads these rows: the current motion, the request, the next) */
    const int orient = (int)ico_tables_count(ICO_TID_motionOrient);
    for (int r = 0; r < orient; r++) {
        const MotionOrientEntry *e = &motionOrient[r];
        const int next = e->nextId;
        int known = next < 0 || next >= rows || next == out->sit || next == out->sit_loop ||
                    next == out->sit_end;

        if (e->id != out->sit && e->id != out->sit_loop && e->id != out->sit_end) {
            continue;
        }
        for (int k = 0; k < out->n_exit && !known; k++) {
            known = out->exit[k] == next;
        }
        if (!known && out->n_exit < ICO_YT_MAX_MOTIONS) {
            out->exit[out->n_exit++] = next;
        }
    }
    return 0;
}

/* 1 when member name is a motion file (the pack loader's .mob, which it
   hands to charFileManager.c ReadMotionFile) */
static int isMotion(const char *name)
{
    size_t n = name != NULL ? strlen(name) : 0;

    return n > 4 && (strcmp(name + n - 4, ".mob") == 0 || strcmp(name + n - 4, ".MOB") == 0);
}

static int wanted(const IcoYtIds *ids, int id)
{
    for (int k = 0; k < ids->n; k++) {
        if (ids->id[k] == id) {
            return 1;
        }
    }
    return 0;
}

typedef struct Collect {
    const IcoYtIds *ids;
    IcoYtSource *out;
    int failed;
} Collect;

static int collect(void *user, int member, const char *name, const IcoDfMember *m,
                   const uint8_t *data)
{
    Collect *c = (Collect *)user;
    IcoYtSource *o = c->out;

    (void)member;
    if (!isMotion(name) || !wanted(c->ids, m->id) || m->size == 0) {
        return 0;
    }
    for (int k = 0; k < o->n; k++) {
        if (o->m[k].id == m->id) {
            return 0; /* the pack's first member of that number is the one the game keeps */
        }
    }
    if (o->n == ICO_YT_MAX_MOTIONS) {
        return 0;
    }
    IcoYtCopy *cp = &o->m[o->n];
    cp->data = (uint8_t *)malloc(m->size);
    if (cp->data == NULL) {
        c->failed = 1;
        return 1;
    }
    memcpy(cp->data, data, m->size);
    cp->id = m->id;
    cp->size = m->size;
    snprintf(cp->name, sizeof cp->name, "%s", name);
    o->bytes += m->size;
    o->n++;
    return o->n == c->ids->n ? 1 : 0;
}

void ico_yorda_trip_source_free(IcoYtSource *src)
{
    if (src == NULL) {
        return;
    }
    for (int k = 0; k < src->n; k++) {
        free(src->m[k].data);
    }
    memset(src, 0, sizeof(*src));
}

int ico_yorda_trip_source_read(struct IcoDfIndex *idx, const IcoYtIds *ids, IcoYtSource *out,
                               char *why, size_t why_size)
{
    IcoDfMember m;
    int pack = -1;
    int count;

    memset(out, 0, sizeof(*out));
    if (idx == NULL || ids == NULL || ids->sit < 0) {
        snprintf(why, why_size, "the disc's DATA.DF or motion table could not be read");
        return -1;
    }
    count = ico_df_index_member_count(idx);
    for (int i = 0; i < count && pack < 0; i++) {
        if (ico_df_index_member_at(idx, i, &m) == 0 && m.id == ids->sit &&
            isMotion(ico_df_index_member_name(idx, i))) {
            pack = m.pack;
        }
    }
    if (pack < 0) {
        snprintf(why, why_size, "no pack of this disc's DATA.DF holds her fall (motion %d)",
                 ids->sit);
        return 1;
    }
    snprintf(out->pack, sizeof out->pack, "%s",
             ico_df_index_entry_name(idx, pack) != NULL ? ico_df_index_entry_name(idx, pack) : "?");
    Collect c = {ids, out, 0};
    int rc = ico_df_index_read_pack(idx, pack, collect, &c);
    if (rc < 0 || c.failed) {
        snprintf(why, why_size, "%s could not be read%s", out->pack,
                 c.failed ? " (out of memory)" : "");
        char keep[sizeof out->pack];
        memcpy(keep, out->pack, sizeof keep);
        ico_yorda_trip_source_free(out);
        memcpy(out->pack, keep, sizeof keep);
        return -1;
    }
    snprintf(why, why_size, "%d of her %d motions read from %s (%u bytes)", out->n, ids->n,
             out->pack, (unsigned)out->bytes);
    return 0;
}
