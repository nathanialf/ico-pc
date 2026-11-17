/*
 * port/save/state_checkpoint.c: where a save state keeps the game's
 * checkpoint, without the game (state_checkpoint.h).
 */
#include "state_checkpoint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "state_registry.h" /* ICO_STATE_AUDIO */
#include "state_xbuild.h"   /* ICO_XB_* */

static const struct {
    const char *name;
    uint32_t size;
} s_globals[ICO_CKPT_G_COUNT] = {
    {"gameSysMainSaveBuff", ICO_CKPT_IMAGE_BYTES},
    {"systemStatus", 48},
    {"IosMcPreviewInfo", 24},
    {"optionScreenMode", 4},
    {"girlControlMode", 4},
    {"stage_no", 4},
};

const char *ico_ckpt_global_name(int g)
{
    return g >= 0 && g < ICO_CKPT_G_COUNT ? s_globals[g].name : "";
}

uint32_t ico_ckpt_global_size(int g)
{
    return g >= 0 && g < ICO_CKPT_G_COUNT ? s_globals[g].size : 0u;
}

static uint32_t rd32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

const IcoCkptLayout *ico_ckpt_table_find(const IcoCkptLayout *tab, int n, const char *commit,
                                         const char *platform)
{
    int i;

    if (tab == NULL || commit == NULL || platform == NULL || commit[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < n; i++) {
        if (tab[i].commit[0] != '\0' && strcmp(tab[i].commit, commit) == 0 &&
            strcmp(tab[i].platform, platform) == 0) {
            return &tab[i];
        }
    }
    return NULL;
}

/* --- the record ----------------------------------------------------------------------- */

size_t ico_ckpt_record_write(const IcoCkptPlace *g, void *out, size_t cap)
{
    unsigned char *p = (unsigned char *)out;
    int i;

    if (p == NULL || cap < ICO_CKPT_RECORD_BYTES) {
        return 0;
    }
    memset(p, 0, ICO_CKPT_RECORD_BYTES);
    wr32(p, ICO_CKPT_RECORD_MAGIC);
    wr32(p + 4, 1u);
    wr32(p + 8, (uint32_t)ICO_CKPT_G_COUNT);
    for (i = 0; i < ICO_CKPT_G_COUNT; i++) {
        unsigned char *e = p + 16 + 32 * (size_t)i;

        memcpy(e, s_globals[i].name, strlen(s_globals[i].name)); /* each name < 20 bytes */
        wr32(e + 20, (uint32_t)g[i].range);
        wr32(e + 24, g[i].off);
        wr32(e + 28, g[i].size);
    }
    return ICO_CKPT_RECORD_BYTES;
}

static void places_none(IcoCkptPlace *g)
{
    int i;

    for (i = 0; i < ICO_CKPT_G_COUNT; i++) {
        g[i].range = ICO_CKPT_RANGE_NONE;
        g[i].off = 0;
        g[i].size = 0;
    }
}

int ico_ckpt_record_read(const void *pv, size_t n, IcoCkptPlace *g)
{
    const unsigned char *p = (const unsigned char *)pv;
    uint32_t count, i;
    int k;

    places_none(g);
    if (p == NULL || n < 16 || rd32(p) != ICO_CKPT_RECORD_MAGIC || rd32(p + 4) != 1u) {
        return -1;
    }
    count = rd32(p + 8);
    if (count > 64 || (uint64_t)n < 16u + 32u * (uint64_t)count) {
        return -1;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *e = p + 16 + 32 * (size_t)i;
        char name[21];
        const int32_t range = (int32_t)rd32(e + 20);

        memcpy(name, e, 20);
        name[20] = '\0';
        for (k = 0; k < ICO_CKPT_G_COUNT; k++) {
            if (strcmp(name, s_globals[k].name) != 0) {
                continue;
            }
            if ((range != 0 && range != 1 && range != ICO_CKPT_RANGE_NONE) ||
                (range != ICO_CKPT_RANGE_NONE && rd32(e + 28) != s_globals[k].size)) {
                places_none(g);
                return -1;
            }
            g[k].range = range;
            g[k].off = range == ICO_CKPT_RANGE_NONE ? 0u : rd32(e + 24);
            g[k].size = range == ICO_CKPT_RANGE_NONE ? 0u : s_globals[k].size;
        }
    }
    return 0;
}

/* --- the disc's image -------------------------------------------------------------------- */

/* gamesys.c stamp_str_* (the Asian disc runs the Korean build's), and the
   discs' stage counts (region.c: 105 European, 103 Japanese, Korean, Asian,
   North American and August 2001 preview, 102 June 2001 demo) */
static const struct {
    const char *slug, *stamp;
    int is2001, last_stage;
} s_discs[] = {
    {"pal", "12/12/01 17:53:37", 0, 105},   {"jpn", "10/23/01 02:02:43", 0, 103},
    {"kor", "10/23/01 17:47:13", 0, 103},   {"cn", "10/23/01 17:47:13", 0, 103},
    {"ntsc", "08/18/01 22:22:20", 1, 103},  {"aug6", "08/07/01 14:51:53", 1, 103},
    {"jun26", "06/26/01 20:00:06", 1, 102},
};

static int disc_index(const char *disc)
{
    size_t i;

    for (i = 0; disc != NULL && i < sizeof s_discs / sizeof s_discs[0]; i++) {
        if (strcmp(disc, s_discs[i].slug) == 0) {
            return (int)i;
        }
    }
    return -1;
}

const char *ico_ckpt_stamp(const char *disc)
{
    const int i = disc_index(disc);

    return i >= 0 ? s_discs[i].stamp : NULL;
}

int ico_ckpt_disc_2001(const char *disc)
{
    const int i = disc_index(disc);

    return i >= 0 && s_discs[i].is2001;
}

int ico_ckpt_disc_last_stage(const char *disc)
{
    const int i = disc_index(disc);

    return i >= 0 ? s_discs[i].last_stage : 0;
}

int ico_ckpt_image_valid(const unsigned char *p, uint64_t avail, const char *disc, int *stage)
{
    const int d = disc_index(disc);
    int32_t st;

    if (stage != NULL) {
        *stage = 0;
    }
    /* the stamp with its NUL: gamesysVersionSave writes 18 bytes */
    if (d < 0 || p == NULL || avail < ICO_CKPT_IMAGE_BYTES ||
        memcmp(p, s_discs[d].stamp, ICO_CKPT_STAMP_BYTES) != 0) {
        return 0;
    }
    /* gflagSave's stage, then (not on the 2001 discs) gFlagGameClear */
    st = (int32_t)rd32(p + ICO_CKPT_STAMP_BYTES);
    if (st < 1 || st > s_discs[d].last_stage) {
        return 0;
    }
    if (!s_discs[d].is2001 && rd32(p + ICO_CKPT_STAMP_BYTES + 4) > 1u) {
        return 0;
    }
    if (stage != NULL) {
        *stage = (int)st;
    }
    return 1;
}

int ico_ckpt_search(const IcoCkptSections *s, const char *disc, IcoCkptPlace *at, int *passed,
                    int *rejected)
{
    const char *stamp = ico_ckpt_stamp(disc);
    const unsigned char *first = NULL;
    int distinct = 0, ok = 0, bad = 0, r;

    at->range = ICO_CKPT_RANGE_NONE;
    at->off = 0;
    at->size = 0;
    for (r = 0; stamp != NULL && r < 2; r++) {
        const unsigned char *p = s->p[r];
        uint64_t i;

        if (p == NULL || s->n[r] < ICO_CKPT_STAMP_BYTES) {
            continue;
        }
        for (i = 0; i + ICO_CKPT_STAMP_BYTES <= s->n[r]; i++) {
            const unsigned char *q =
                (const unsigned char *)memchr(p + i, (unsigned char)stamp[0], s->n[r] - i);

            if (q == NULL) {
                break;
            }
            i = (uint64_t)(q - p);
            if (i + ICO_CKPT_STAMP_BYTES > s->n[r] || memcmp(q, stamp, ICO_CKPT_STAMP_BYTES) != 0) {
                continue;
            }
            if (!ico_ckpt_image_valid(q, s->n[r] - i, disc, NULL)) {
                bad++;
                continue;
            }
            ok++;
            if (first == NULL) {
                first = q;
                distinct = 1;
                at->range = r;
                at->off = (uint32_t)i;
                at->size = ICO_CKPT_IMAGE_BYTES;
            } else if (memcmp(first, q, ICO_CKPT_IMAGE_BYTES) != 0) {
                distinct++;
            }
        }
    }
    if (passed != NULL) {
        *passed = ok;
    }
    if (rejected != NULL) {
        *rejected = bad;
    }
    if (distinct == 1) {
        return ICO_CKPT_SEARCH_ONE;
    }
    at->range = ICO_CKPT_RANGE_NONE;
    return distinct == 0 ? ICO_CKPT_SEARCH_NONE : ICO_CKPT_SEARCH_SEVERAL;
}

/* --- the decision ---------------------------------------------------------------------------- */

static int all_zero(const unsigned char *p, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (p[i] != 0) {
            return 0;
        }
    }
    return 1;
}

/* Whether places g fit the state: 0 with plan's globals, checkpoint and
   stage filled, or -1 with why. */
static int confirm(const IcoCkptInput *in, const IcoCkptPlace *g, IcoCkptPlan *plan, char *why,
                   size_t n)
{
    const unsigned char *at[ICO_CKPT_G_COUNT];
    int k, stage = 0, valid;
    uint32_t w3 = 0;

    for (k = 0; k < ICO_CKPT_G_COUNT; k++) {
        at[k] = NULL;
        if (g[k].range == ICO_CKPT_RANGE_NONE) {
            continue;
        }
        if ((g[k].range != 0 && g[k].range != 1) || g[k].size != s_globals[k].size ||
            in->sec.p[g[k].range] == NULL || g[k].off > in->sec.n[g[k].range] ||
            in->sec.n[g[k].range] - g[k].off < g[k].size) {
            snprintf(why, n, "%s (%u bytes at %s + 0x%x) lies outside the state's section",
                     s_globals[k].name, g[k].size, g[k].range == 1 ? "gbss" : "gdata", g[k].off);
            return -1;
        }
        at[k] = in->sec.p[g[k].range] + g[k].off;
    }
    if (at[ICO_CKPT_G_IMAGE] == NULL) {
        snprintf(why, n, "it names no place for the save image");
        return -1;
    }
    if (at[ICO_CKPT_G_STAGE] != NULL && (int32_t)rd32(at[ICO_CKPT_G_STAGE]) != in->stage_no) {
        snprintf(why, n, "stage_no there reads %d where the header says %d",
                 (int)(int32_t)rd32(at[ICO_CKPT_G_STAGE]), in->stage_no);
        return -1;
    }
    if (at[ICO_CKPT_G_STATUS] != NULL) {
        w3 = rd32(at[ICO_CKPT_G_STATUS] + 12);
    }
    valid = ico_ckpt_image_valid(at[ICO_CKPT_G_IMAGE],
                                 in->sec.n[g[ICO_CKPT_G_IMAGE].range] - g[ICO_CKPT_G_IMAGE].off,
                                 in->disc, &stage);
    if (!valid && !(at[ICO_CKPT_G_STATUS] != NULL && w3 == 0 &&
                    all_zero(at[ICO_CKPT_G_IMAGE], ICO_CKPT_STAMP_BYTES))) {
        snprintf(why, n, "the bytes there are not a save image of the %s disc", in->disc);
        return -1;
    }
    for (k = 0; k < ICO_CKPT_G_COUNT; k++) {
        plan->g[k] = at[k];
        plan->place[k] = g[k];
    }
    plan->checkpoint = at[ICO_CKPT_G_STATUS] == NULL ? -1 : w3 != 0;
    if (!valid) {
        plan->checkpoint = 0; /* no image at all: nothing was ever saved */
    }
    plan->image_stage = stage;
    return 0;
}

static int same_place(const IcoCkptPlace *a, const IcoCkptPlace *b)
{
    return a->range == b->range && (a->range == ICO_CKPT_RANGE_NONE || a->off == b->off);
}

int ico_ckpt_locate(const IcoCkptInput *in, IcoCkptPlan *plan)
{
    char why[160];
    IcoCkptPlace g[ICO_CKPT_G_COUNT];
    const IcoCkptLayout *e;
    int i, k;

    memset(plan, 0, sizeof *plan);
    places_none(plan->place);
    plan->checkpoint = -1;
    why[0] = '\0';
    if (ico_ckpt_stamp(in->disc) == NULL) {
        snprintf(plan->why, sizeof plan->why, "the disc \"%s\" is not one this knows",
                 in->disc != NULL ? in->disc : "");
        return -1;
    }
    /* 1: the state's own record */
    if (in->record != NULL) {
        if (ico_ckpt_record_read(in->record, in->record_n, g) == 0 &&
            confirm(in, g, plan, why, sizeof why) == 0) {
            plan->by = ICO_CKPT_BY_RECORD;
            snprintf(plan->source, sizeof plan->source, "the state's own record");
            return 0;
        }
        snprintf(plan->why, sizeof plan->why, "its record: %s", why[0] != '\0' ? why : "damaged");
    }
    /* 2: the table's entry of its build */
    e = ico_ckpt_table_find(in->table, in->table_n, in->commit, in->platform);
    if (e != NULL) {
        if (e->gdata_bytes == in->sec.n[0] && e->gbss_bytes == in->sec.n[1] &&
            confirm(in, e->g, plan, why, sizeof why) == 0) {
            plan->by = ICO_CKPT_BY_TABLE;
            snprintf(plan->source, sizeof plan->source, "the table's %s %s (%s)", e->tag,
                     e->platform, e->asset);
            return 0;
        }
        snprintf(plan->why, sizeof plan->why, "the table's %s %s: %s", e->tag, e->platform,
                 e->gdata_bytes != in->sec.n[0] || e->gbss_bytes != in->sec.n[1]
                     ? "its game ranges are of other sizes"
                     : why);
    }
    /* 3: a layout of the same platform and sizes that its bytes confirm */
    {
        const IcoCkptLayout *first = NULL;
        IcoCkptPlan p0;
        int agree = 1, n = 0;

        memset(&p0, 0, sizeof p0);
        for (i = -1; i < in->table_n; i++) {
            const IcoCkptLayout *c = i < 0 ? in->self : &in->table[i];
            IcoCkptPlan t;

            if (c == NULL || in->platform == NULL || strcmp(c->platform, in->platform) != 0 ||
                c->gdata_bytes != in->sec.n[0] || c->gbss_bytes != in->sec.n[1]) {
                continue;
            }
            memset(&t, 0, sizeof t);
            if (confirm(in, c->g, &t, why, sizeof why) != 0) {
                continue;
            }
            n++;
            if (first == NULL) {
                first = c;
                p0 = t;
            } else if (!same_place(&first->g[ICO_CKPT_G_IMAGE], &c->g[ICO_CKPT_G_IMAGE]) ||
                       !same_place(&first->g[ICO_CKPT_G_STATUS], &c->g[ICO_CKPT_G_STATUS])) {
                agree = 0;
            }
        }
        if (first != NULL && agree) {
            for (k = 0; k < ICO_CKPT_G_COUNT; k++) {
                plan->g[k] = p0.g[k];
                plan->place[k] = p0.place[k];
            }
            plan->checkpoint = p0.checkpoint;
            plan->image_stage = p0.image_stage;
            plan->by = ICO_CKPT_BY_LAYOUT;
            plan->layouts = n;
            snprintf(plan->source, sizeof plan->source,
                     "the layout of %s %s, which its bytes confirm (%d layout%s agree)",
                     first == in->self ? "this version" : first->tag, first->platform, n,
                     n == 1 ? "" : "s");
            return 0;
        }
        if (first != NULL) {
            snprintf(plan->why, sizeof plan->why,
                     "%d released layouts fit its bytes and put the image in different places", n);
        }
    }
    /* 4: the image by its contents */
    {
        IcoCkptPlace at;
        const int r = ico_ckpt_search(&in->sec, in->disc, &at, &plan->passed, &plan->rejected);

        if (r == ICO_CKPT_SEARCH_ONE) {
            places_none(plan->place);
            plan->place[ICO_CKPT_G_IMAGE] = at;
            for (k = 0; k < ICO_CKPT_G_COUNT; k++) {
                plan->g[k] = NULL;
            }
            plan->g[ICO_CKPT_G_IMAGE] = in->sec.p[at.range] + at.off;
            (void)ico_ckpt_image_valid(plan->g[ICO_CKPT_G_IMAGE], in->sec.n[at.range] - at.off,
                                       in->disc, &plan->image_stage);
            plan->checkpoint = -1;
            plan->by = ICO_CKPT_BY_SEARCH;
            snprintf(plan->source, sizeof plan->source,
                     "its bytes: one save image of the %s disc at %s + 0x%x (%d passed, %d "
                     "stamps rejected)",
                     in->disc, at.range == 1 ? "gbss" : "gdata", at.off, plan->passed,
                     plan->rejected);
            return 0;
        }
        snprintf(plan->why + strlen(plan->why), sizeof plan->why - strlen(plan->why),
                 "%sits bytes hold %s (%d stamps rejected)", plan->why[0] != '\0' ? "; " : "",
                 r == ICO_CKPT_SEARCH_NONE ? "no save image of its disc"
                                           : "several save images that differ",
                 plan->rejected);
    }
    return -1;
}

/* --- the route -------------------------------------------------------------------------------- */

IcoCkptRoute ico_ckpt_route(IcoStateResult r, int other_version)
{
    switch (r) {
    case ICO_STATE_OK:
        return ICO_CKPT_ROUTE_EXACT;
    /* another version's or another platform's state: nothing of it can
       resume here, its checkpoint can */
    case ICO_STATE_OTHER_BUILD:
    case ICO_STATE_OTHER_PLATFORM:
    /* the restore failed its checks (the backup is back) or a section is
       damaged: the checkpoint needs only the game's two sections */
    case ICO_STATE_SELF_CHECK:
    case ICO_STATE_BAD_FILE:
        return ICO_CKPT_ROUTE_CHECKPOINT;
    /* settings: another version's state differs in more than them; this
       version's resumes exactly with the setting the box names put back */
    case ICO_STATE_OTHER_SETTINGS:
        return other_version ? ICO_CKPT_ROUTE_CHECKPOINT : ICO_CKPT_ROUTE_REFUSED;
    /* by rule: another disc's journey, a Permadeath journey */
    case ICO_STATE_OTHER_DISC:
    case ICO_STATE_PERMADEATH:
    /* nothing there, or the moment: tried again later, as before */
    case ICO_STATE_EMPTY:
    case ICO_STATE_OFF:
    case ICO_STATE_NOT_PAUSED:
    case ICO_STATE_STAGE_SWITCH:
    case ICO_STATE_CARD_BUSY:
    case ICO_STATE_DISC_BUSY:
    case ICO_STATE_MOVIE:
    case ICO_STATE_PHOTO:
    case ICO_STATE_IO:
    case ICO_STATE_BUSY:
        return ICO_CKPT_ROUTE_REFUSED;
    }
    return ICO_CKPT_ROUTE_REFUSED;
}

IcoStateResult ico_ckpt_xb_result(int xb_kind)
{
    switch (xb_kind) {
    case ICO_XB_OK:
        return ICO_STATE_OK;
    case ICO_XB_BAD:
        return ICO_STATE_BAD_FILE;
    }
    return ICO_STATE_OTHER_BUILD; /* CODE, DATA, BLOCKS, PINNED and any later kind */
}

IcoCkptBox ico_ckpt_box(int is_load, IcoStateResult r, int via_checkpoint, IcoStateBlocker blocker)
{
    if (r == ICO_STATE_OK) {
        return is_load && via_checkpoint ? ICO_CKPT_BOX_NOTICE : ICO_CKPT_BOX_DONE;
    }
    if (r == ICO_STATE_OTHER_SETTINGS) {
        return ICO_CKPT_BOX_SETTINGS;
    }
    if (r == ICO_STATE_OTHER_BUILD && is_load && blocker != ICO_STATE_BLOCK_NONE) {
        return ICO_CKPT_BOX_VERSION;
    }
    return ICO_CKPT_BOX_REFUSAL;
}

const char *ico_ckpt_route_text(IcoCkptRoute k)
{
    switch (k) {
    case ICO_CKPT_ROUTE_EXACT:
        return "exact";
    case ICO_CKPT_ROUTE_CHECKPOINT:
        return "checkpoint";
    case ICO_CKPT_ROUTE_REFUSED:
        return "refused";
    }
    return "?";
}

/* --- the syms section's blocks ------------------------------------------------------------- */

/* state_xbuild.c's record: the header words (H_*), the record sizes */
enum {
    SY_MAGIC = 0,
    SY_VERSION = 4,
    SY_UNITS = 20,
    SY_DATA = 24,
    SY_POINTS = 28,
    SY_BLOCKS = 32,
    SY_STR = 36,
    SY_LAYOUT = 40,
    SY_BYTES = 64,
    SY_POOL = 64,
    SY_LIST = 68,
    SY_PAGES = 72,
    SY_PRODS = 76,
    SY_BYTES3 = 80
};

#define SY_MAGIC_WORD 0x534D5953u /* "SYMS" */

int ico_ckpt_syms_block(const void *sv, size_t n, const char *name, uint64_t *off, uint64_t *size,
                        unsigned *flags, uint64_t *layout)
{
    const unsigned char *s = (const unsigned char *)sv;
    uint32_t ver, nu, nd, np, nb, ns, npool = 0, nl = 0, npg = 0, npr = 0, i;
    uint64_t hb, db, pb, total;
    const unsigned char *bl;
    const char *str;

    if (s == NULL || n < SY_BYTES || rd32(s + SY_MAGIC) != SY_MAGIC_WORD) {
        return -1;
    }
    ver = rd32(s + SY_VERSION);
    if (ver < 1 || ver > 3 || (ver >= 3 && n < SY_BYTES3)) {
        return -1;
    }
    hb = ver >= 3 ? SY_BYTES3 : SY_BYTES;
    db = ver >= 2 ? 48u : 40u;
    pb = ver >= 3 ? 64u : 48u;
    nu = rd32(s + SY_UNITS);
    nd = rd32(s + SY_DATA);
    np = rd32(s + SY_POINTS);
    nb = rd32(s + SY_BLOCKS);
    ns = rd32(s + SY_STR);
    if (ver >= 3) {
        npool = rd32(s + SY_POOL);
        nl = rd32(s + SY_LIST);
        npg = rd32(s + SY_PAGES);
        npr = rd32(s + SY_PRODS);
    }
    total = hb + (uint64_t)nu * 48u + (uint64_t)nd * db + (uint64_t)np * pb + (uint64_t)nb * 40u +
            (uint64_t)nl * 40u + (uint64_t)npg * 16u + (uint64_t)npr * 24u + ns + npool;
    if (ns == 0 || total != n) {
        return -1;
    }
    bl = s + hb + (uint64_t)nu * 48u + (uint64_t)nd * db + (uint64_t)np * pb;
    str = (const char *)(bl + (uint64_t)nb * 40u + (uint64_t)nl * 40u + (uint64_t)npg * 16u +
                         (uint64_t)npr * 24u);
    if (str[ns - 1] != '\0') {
        return -1;
    }
    if (layout != NULL) {
        *layout = (uint64_t)rd32(s + SY_LAYOUT) | (uint64_t)rd32(s + SY_LAYOUT + 4) << 32;
    }
    for (i = 0; i < nb; i++) {
        const unsigned char *b = bl + (size_t)i * 40u;
        const uint32_t at = rd32(b + 28);

        if (at < ns && strcmp(str + at, name) == 0) {
            *size = (uint64_t)rd32(b + 8) | (uint64_t)rd32(b + 12) << 32;
            *off = (uint64_t)rd32(b + 16) | (uint64_t)rd32(b + 20) << 32;
            *flags = rd32(b + 24);
            return 1;
        }
    }
    return 0;
}

/* --- a state file ---------------------------------------------------------------------------- */

static const char *const s_sec_names[ICO_CKPT_S_COUNT] = {"gdata", "gbss", "reg", "syms", "ckpt"};

typedef struct ReadCtx {
    IcoCkptState *st;
    int bad;
} ReadCtx;

/* a section's bound (state_checkpoint.h "What a state's sections can hold") */
static uint64_t section_max(int i)
{
    switch (i) {
    case ICO_CKPT_S_GDATA:
    case ICO_CKPT_S_GBSS:
    case ICO_CKPT_S_REG:
        return ICO_CKPT_EE_RAM;
    case ICO_CKPT_S_SYMS:
        return ICO_XB_MAX_BYTES;
    }
    return 4096u;
}

static void *want_section(void *user, const IcoStateSectionInfo *s)
{
    ReadCtx *c = (ReadCtx *)user;
    int i;

    for (i = 0; i < ICO_CKPT_S_COUNT; i++) {
        if (strcmp(s->name, s_sec_names[i]) != 0 || c->st->p[i] != NULL) {
            continue;
        }
        if (s->raw_bytes > section_max(i) ||
            ((i == ICO_CKPT_S_GDATA || i == ICO_CKPT_S_GBSS) &&
             s->raw_bytes + c->st->n[ICO_CKPT_S_GDATA] + c->st->n[ICO_CKPT_S_GBSS] >
                 ICO_CKPT_EE_RAM)) {
            c->bad = 1;
            snprintf(c->st->why, sizeof c->st->why,
                     "its %s section is %llu bytes, more than a state holds (at most %llu%s)",
                     s->name, (unsigned long long)s->raw_bytes, (unsigned long long)section_max(i),
                     i == ICO_CKPT_S_GDATA || i == ICO_CKPT_S_GBSS ? ", the game's two together"
                                                                   : "");
            return NULL;
        }
        c->st->p[i] = (unsigned char *)malloc(s->raw_bytes != 0 ? (size_t)s->raw_bytes : 1u);
        c->st->n[i] = s->raw_bytes;
        if (c->st->p[i] == NULL) {
            c->bad = 1;
            snprintf(c->st->why, sizeof c->st->why, "no memory for its %s section (%llu bytes)",
                     s->name, (unsigned long long)s->raw_bytes);
        }
        return c->st->p[i];
    }
    return NULL;
}

int ico_ckpt_state_read(const char *path, IcoCkptState *st)
{
    ReadCtx c;
    IcoStateResult r;

    memset(st, 0, sizeof *st);
    c.st = st;
    c.bad = 0;
    r = ico_state_file_read(path, &st->h, want_section, &c);
    if (r != ICO_STATE_OK || c.bad) {
        if (st->why[0] == '\0') {
            snprintf(st->why, sizeof st->why, "%s",
                     r == ICO_STATE_EMPTY ? "there is no file"
                                          : "the file or one of the sections it needs is damaged");
        }
        return -1;
    }
    if (st->p[ICO_CKPT_S_GDATA] == NULL) {
        snprintf(st->why, sizeof st->why, "it has no gdata section");
        return -1;
    }
    return 0;
}

void ico_ckpt_state_free(IcoCkptState *st)
{
    int i;

    for (i = 0; i < ICO_CKPT_S_COUNT; i++) {
        free(st->p[i]);
        st->p[i] = NULL;
        st->n[i] = 0;
    }
}

void ico_ckpt_state_input(const IcoCkptState *st, const IcoCkptLayout *self, IcoCkptInput *in)
{
    memset(in, 0, sizeof *in);
    in->commit = st->h.commit;
    in->platform = st->h.platform;
    in->disc = st->h.disc;
    in->stage_no = st->h.stage_no;
    in->sec.p[0] = st->p[ICO_CKPT_S_GDATA];
    in->sec.n[0] = st->n[ICO_CKPT_S_GDATA];
    in->sec.p[1] = st->p[ICO_CKPT_S_GBSS];
    in->sec.n[1] = st->n[ICO_CKPT_S_GBSS];
    in->record = st->p[ICO_CKPT_S_CKPT];
    in->record_n = (size_t)st->n[ICO_CKPT_S_CKPT];
    in->table = ico_ckpt_table;
    in->table_n = ico_ckpt_table_count;
    in->self = self;
}

int ico_ckpt_state_block(const IcoCkptState *st, const char *name, uint64_t size,
                         const unsigned char **out, char *why, size_t n)
{
    uint64_t off = 0, have = 0;
    unsigned flags = 0;
    int k;

    *out = NULL;
    if (st->p[ICO_CKPT_S_SYMS] == NULL || st->p[ICO_CKPT_S_REG] == NULL) {
        return 0;
    }
    k = ico_ckpt_syms_block(st->p[ICO_CKPT_S_SYMS], (size_t)st->n[ICO_CKPT_S_SYMS], name, &off,
                            &have, &flags, NULL);
    if (k <= 0) {
        if (k < 0) {
            snprintf(why, n, "its symbols record is damaged");
        }
        return k;
    }
    if (have != size || (flags & ICO_STATE_AUDIO) != 0 || off > st->n[ICO_CKPT_S_REG] ||
        st->n[ICO_CKPT_S_REG] - off < size) {
        snprintf(why, n, "its block %s is %llu bytes%s, this version's %llu", name,
                 (unsigned long long)have, (flags & ICO_STATE_AUDIO) != 0 ? " (audio)" : "",
                 (unsigned long long)size);
        return -1;
    }
    *out = st->p[ICO_CKPT_S_REG] + off;
    return 1;
}
