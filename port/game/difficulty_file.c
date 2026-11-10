/*
 * port/game/difficulty_file.c
 *
 * The difficulty sidecar (difficulty.h "the sidecar"): difficulty.toml in the
 * per-user folder, beside achievements.toml (ico_host_pref_dir, as
 * achievements.c finds its own), one entry per save and the list of ended
 * journeys:
 *
 *   version = 1
 *   [save]
 *   slot_3_sum = 123456                 # the game block's checksum (mc.sum)
 *   slot_3_preview = "12:345678:2"      # stage:play frames:couch (the product file)
 *   slot_3_journey = "9f3a6c0e5d2b7a11"
 *   slot_3_mode = "hard"                # easy | normal | hard | custom
 *   slot_3_origin = "new"               # new | config | state
 *   slot_3_rows = "shadow_speed=faster ... permadeath=false"
 *   slot_3_knocks = 1                   # the whole-journey count at this save
 *   [finished]
 *   9f3a6c0e5d2b7a11 = true
 *
 * An entry's key is the disc's key prefix (port/core/region.h key_prefix:
 * "" on the European disc, "jpn_", "ntsc_", ...), the card ("" for port 0,
 * "b_" for port 1: with Second card on both cards have a slot 3) and slot_N.
 * Not config.toml: journey data, no ini override layer, and the finished
 * list must outlive any settings reset.  The card files are never touched.
 *
 * Every call reads the file afresh (a save, a load, a Continue confirm: a
 * few a session) and every change is written at once and atomically (the
 * file's .tmp, then a rename over it: ico_toml_write_text), so a crash
 * leaves the old file or the new one, and a restore from a backup
 * (backup.c holds the file) is seen at the next call.
 *
 * A file whose version is missing or not 1 (damaged, or written by a later
 * version) reads as empty, with a log line, and is not written over until
 * the next save or a Permadeath end: then its text is kept as
 * difficulty.toml.bad and a new file is written.  The carry-over of a load
 * is not written into such a file (the save after it writes it).
 *
 * In ico_options with difficulty.c, so it may use only what that library and
 * the host_config.c every one of its programs compiles provide (the config
 * store, ico_region, ico_host_pref_dir, the host_config.h toml calls).
 *
 * Log lines (each on its event): every lookup's outcome ("found by slot",
 * "found by sum", "carry-over", "none"), every write and its failure, every
 * refusal (the Continue list, the load's backstop), a damaged file (the
 * first 8 reads, then every 100th).
 *
 * Tests: port/game/test/difficulty_file_test.c.
 */
#include "difficulty.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/region.h"
#include "../platform/host_config.h"

#define FILE_VERSION 1
#define FILE_NAME "difficulty.toml"
/* the card slots an entry may name (the game's ten files; achievements.c
   and options.c allow the same 0..99) */
#define SLOT_MAX 100
#define PORT_MAX 2

static char s_dir[ICO_PATH_MAX]; /* "" until file_init names one: the per-user folder */
static unsigned s_damaged_reads;

/* --- the file ------------------------------------------------------------------- */

static int file_path(char *out, size_t size)
{
    char dir[ICO_PATH_MAX];

    if (s_dir[0] != '\0') {
        snprintf(dir, sizeof(dir), "%s", s_dir);
    } else if (ico_host_pref_dir(dir, sizeof(dir)) != 0) {
        snprintf(dir, sizeof(dir), "%s", ".");
    }
    return ico_path_join(out, size, dir, FILE_NAME);
}

/* The file as read now.  text: the whole file (NULL: none); t: its entries
   (empty for no file or a damaged one); damaged: the text is no version 1
   file. */
typedef struct {
    char path[ICO_PATH_MAX];
    char *text;
    IcoToml *t;
    int damaged;
} File;

static void file_close(File *f)
{
    free(f->text);
    ico_toml_free(f->t);
    f->text = NULL;
    f->t = NULL;
}

/* 0, or -1 (no path or no memory; file_close is due either way) */
static int file_open(File *f)
{
    memset(f, 0, sizeof(*f));
    if (file_path(f->path, sizeof(f->path)) != 0) {
        fprintf(stderr, "difficulty: the folder's path is too long for " FILE_NAME "\n");
        return -1;
    }
    f->text = ico_toml_read_text(f->path);
    if (f->text != NULL) {
        const char *p = f->text;
        IcoToml *t;

        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            p++;
        }
        t = ico_toml_parse(f->text);
        if (t == NULL) {
            return -1;
        }
        if (*p != '\0' && ico_toml_get_int(t, "version", -1) != FILE_VERSION) {
            const char *v = ico_toml_get(t, "version");
            const unsigned n = ++s_damaged_reads;

            f->damaged = 1;
            if (n <= 8 || n % 100 == 0) {
                fprintf(stderr,
                        "difficulty: %s is damaged or from a later version (version %s): read "
                        "as empty, not written over until the next save (read %u)\n",
                        f->path, v != NULL ? v : "missing", n);
            }
            ico_toml_free(t);
            t = ico_toml_parse("");
        }
        f->t = t;
    } else {
        f->t = ico_toml_parse("");
    }
    return f->t != NULL ? 0 : -1;
}

/* Writes f->t over the file's text atomically; a damaged file's text is kept
   as <file>.bad first and the new file starts empty.  0, or -1 (logged). */
static int file_commit(File *f, const char *what)
{
    char *text;
    int rc;

    if (f->damaged && f->text != NULL) {
        char bad[ICO_PATH_MAX + 8];

        snprintf(bad, sizeof(bad), "%s.bad", f->path);
        if (ico_toml_write_text(f->text, bad) == 0) {
            fprintf(stderr, "difficulty: the damaged %s kept as %s; a new file is written\n",
                    f->path, bad);
        } else {
            fprintf(stderr, "difficulty: cannot keep the damaged %s as %s; a new file is written\n",
                    f->path, bad);
        }
        free(f->text);
        f->text = NULL;
        f->damaged = 0;
    }
    ico_toml_set_int(f->t, "version", FILE_VERSION);
    text = ico_toml_render(f->t, f->text);
    if (text == NULL) {
        fprintf(stderr, "difficulty: no memory to write %s (%s)\n", f->path, what);
        return -1;
    }
    rc = ico_toml_write_text(text, f->path);
    free(text);
    if (rc != 0) {
        fprintf(stderr, "difficulty: cannot write %s (%s); the old file is kept\n", f->path, what);
        return -1;
    }
    fprintf(stderr, "difficulty: wrote %s (%s)\n", f->path, what);
    return 0;
}

/* --- the entries ------------------------------------------------------------------ */

static int slot_ok(int port, int slot)
{
    return port >= 0 && port < PORT_MAX && slot >= 0 && slot < SLOT_MAX;
}

/* "save.<disc prefix><card>slot_N_<field>": 0, or -1 */
static int entry_key(int port, int slot, const char *field, char *out, size_t size)
{
    char base[64];

    snprintf(base, sizeof(base), "%sslot_%d_%s", port == 1 ? "b_" : "", slot, field);
    return ico_region_key_prefixed("save", base, out, (int)size);
}

static const char *entry_get(const File *f, int port, int slot, const char *field)
{
    char key[96];

    return entry_key(port, slot, field, key, sizeof(key)) == 0 ? ico_toml_get(f->t, key) : NULL;
}

/* the entry's checksum, or -1 for no entry */
static long long entry_sum(const File *f, int port, int slot)
{
    char key[96];

    if (entry_key(port, slot, "sum", key, sizeof(key)) != 0) {
        return -1;
    }
    return ico_toml_get_int(f->t, key, -1);
}

static void preview_text(int stage, uint32_t play, int sofa, char *out, size_t size)
{
    snprintf(out, size, "%d:%u:%d", stage, (unsigned)play, sofa);
}

/* 16 hex digits into two words; 0, or -1 */
static int journey_parse(const char *s, uint32_t out[2])
{
    char half[9];

    if (s == NULL || strlen(s) != 16) {
        return -1;
    }
    for (int i = 0; i < 16; i++) {
        const char c = s[i];

        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return -1;
        }
    }
    memcpy(half, s, 8);
    half[8] = '\0';
    out[0] = (uint32_t)strtoul(half, NULL, 16);
    memcpy(half, s + 8, 8);
    out[1] = (uint32_t)strtoul(half, NULL, 16);
    return 0;
}

static int journey_finished(const File *f, const char *id)
{
    char key[40];

    if (id == NULL || strlen(id) != 16 || strcmp(id, "0000000000000000") == 0) {
        return 0;
    }
    snprintf(key, sizeof(key), "finished.%s", id);
    return ico_toml_get_bool(f->t, key, 0) != 0;
}

/* The entry at (port, slot) as a record: 0, or -1 (no rows or no journey id:
   logged) */
static int entry_record(const File *f, int port, int slot, IcoDifficulty *out)
{
    const char *rows = entry_get(f, port, slot, "rows");
    const char *journey = entry_get(f, port, slot, "journey");
    const char *mode = entry_get(f, port, slot, "mode");
    const char *origin = entry_get(f, port, slot, "origin");
    uint32_t id[2];
    char key[96];
    long long knocks;
    int bad, m, o;

    if (rows == NULL || journey_parse(journey, id) != 0) {
        fprintf(stderr, "difficulty: the entry of card %d slot %d has %s: not used\n", port + 1,
                slot, rows == NULL ? "no rows" : "no journey id");
        return -1;
    }
    ico_difficulty_preset(ICO_DIFF_NORMAL, out);
    out->journey[0] = id[0];
    out->journey[1] = id[1];
    bad = ico_difficulty_rows_parse(rows, out);
    if (bad != 0) {
        fprintf(stderr,
                "difficulty: the entry of card %d slot %d: %d word(s) of its rows are no value "
                "(above): those rows are Normal's\n",
                port + 1, slot, bad);
    }
    m = ico_difficulty_mode_parse(mode);
    out->mode = m >= 0 ? m : (int)ico_difficulty_classify(out);
    o = ico_difficulty_origin_parse(origin);
    out->origin = o > ICO_DIFF_ORIGIN_NONE ? o : ICO_DIFF_ORIGIN_NEW;
    entry_key(port, slot, "knocks", key, sizeof(key));
    knocks = ico_toml_get_int(f->t, key, 0);
    out->knocks_journey = knocks < 0 ? 0 : knocks > 1000000 ? 1000000 : (int32_t)knocks;
    out->finished = journey_finished(f, journey);
    return 0;
}

static void entry_write(File *f, int port, int slot, const IcoDifficulty *d, uint32_t sum,
                        const char *preview)
{
    char key[96];
    char rows[ICO_DIFF_ROWS_TEXT_MAX];
    char id[20];

    ico_difficulty_rows_format(d, rows, sizeof(rows));
    ico_difficulty_journey_text(d, id, sizeof(id));
    entry_key(port, slot, "sum", key, sizeof(key));
    ico_toml_set_int(f->t, key, (long long)sum);
    /* no preview (a load's carry-over): "", so an earlier save's preview in
       this slot is not taken for this journey's */
    entry_key(port, slot, "preview", key, sizeof(key));
    ico_toml_set_string(f->t, key, preview != NULL ? preview : "");
    entry_key(port, slot, "journey", key, sizeof(key));
    ico_toml_set_string(f->t, key, id);
    entry_key(port, slot, "mode", key, sizeof(key));
    ico_toml_set_string(f->t, key, ico_difficulty_mode_name((IcoDiffMode)d->mode));
    entry_key(port, slot, "origin", key, sizeof(key));
    ico_toml_set_string(f->t, key, ico_difficulty_origin_name((IcoDiffOrigin)d->origin));
    entry_key(port, slot, "rows", key, sizeof(key));
    ico_toml_set_string(f->t, key, rows);
    entry_key(port, slot, "knocks", key, sizeof(key));
    ico_toml_set_int(f->t, key, d->knocks_journey);
}

static const char *mode_shown(int mode)
{
    static const char *const k[] = {"Easy", "Normal", "Hard", "Custom"};

    return mode >= 0 && mode < ICO_DIFF_MODE_COUNT ? k[mode] : "Custom";
}

static const char *origin_word(int origin)
{
    return ico_difficulty_origin_name((IcoDiffOrigin)origin);
}

/* --- the calls -------------------------------------------------------------------- */

void ico_difficulty_file_init(const char *dir)
{
    snprintf(s_dir, sizeof(s_dir), "%s", dir != NULL ? dir : "");
    s_damaged_reads = 0;
}

void ico_difficulty_slot_saved(int port, int slot, uint32_t sum, int stage, uint32_t play, int sofa)
{
    File f;
    const IcoDifficulty *d;
    char preview[48], id[20], what[192];

    ico_difficulty_set_run(port, slot, sum);
    if (!slot_ok(port, slot)) {
        fprintf(stderr, "difficulty: card %d slot %d is not a save slot: no entry written\n",
                port + 1, slot);
        return;
    }
    if (file_open(&f) != 0) {
        file_close(&f);
        return;
    }
    d = ico_difficulty_active();
    preview_text(stage, play, sofa, preview, sizeof(preview));
    ico_difficulty_journey_text(d, id, sizeof(id));
    entry_write(&f, port, slot, d, sum, preview);
    snprintf(what, sizeof(what),
             "card %d slot %d saved: %s, journey %s, origin %s, sum %u, preview %s, knock-downs "
             "%d",
             port + 1, slot, mode_shown(d->mode), id, origin_word(d->origin), (unsigned)sum,
             preview, (int)d->knocks_journey);
    file_commit(&f, what);
    file_close(&f);
}

void ico_difficulty_slot_loaded(int port, int slot, uint32_t sum)
{
    File f;
    IcoDifficulty d;
    int found_port = -1, found_slot = -1;
    char id[20];

    if (!slot_ok(port, slot)) {
        fprintf(stderr,
                "difficulty: load of card %d slot %d: none (not a save slot); the record is "
                "unchanged\n",
                port + 1, slot);
        return;
    }
    if (file_open(&f) != 0) {
        file_close(&f);
        fprintf(stderr,
                "difficulty: load of card %d slot %d: none (" FILE_NAME
                " cannot be read); the record is unchanged\n",
                port + 1, slot);
        return;
    }
    if (entry_sum(&f, port, slot) == (long long)sum && entry_record(&f, port, slot, &d) == 0) {
        found_port = port;
        found_slot = slot;
    }
    for (int p = 0; found_port < 0 && p < PORT_MAX; p++) {
        for (int s = 0; s < SLOT_MAX; s++) {
            if ((p != port || s != slot) && entry_sum(&f, p, s) == (long long)sum &&
                entry_record(&f, p, s, &d) == 0) {
                found_port = p;
                found_slot = s;
                break;
            }
        }
    }
    if (found_port >= 0) {
        ico_difficulty_journey_text(&d, id, sizeof(id));
        if (found_port == port && found_slot == slot) {
            fprintf(stderr,
                    "difficulty: load of card %d slot %d (sum %u): found by slot: %s, journey "
                    "%s, origin %s, knock-downs %d%s\n",
                    port + 1, slot, (unsigned)sum, mode_shown(d.mode), id, origin_word(d.origin),
                    (int)d.knocks_journey, d.finished ? ", the journey has ended" : "");
        } else {
            fprintf(stderr,
                    "difficulty: load of card %d slot %d (sum %u): found by sum at card %d "
                    "slot %d (the file was moved or copied): %s, journey %s, origin %s, "
                    "knock-downs %d%s\n",
                    port + 1, slot, (unsigned)sum, found_port + 1, found_slot, mode_shown(d.mode),
                    id, origin_word(d.origin), (int)d.knocks_journey,
                    d.finished ? ", the journey has ended" : "");
        }
        d.run_port = port;
        d.run_slot = slot;
        d.run_has_slot = 1;
        d.run_sum = sum;
        ico_difficulty_set_active(&d);
        file_close(&f);
        return;
    }
    /* no record: the one-time carry-over of the retired config keys, kept at
       once so the next load finds it (a save made before the file existed
       and a slot state_recover rebuilt come here once) */
    ico_difficulty_from_config(&d);
    d.run_port = port;
    d.run_slot = slot;
    d.run_has_slot = 1;
    d.run_sum = sum;
    ico_difficulty_journey_text(&d, id, sizeof(id));
    fprintf(stderr,
            "difficulty: load of card %d slot %d (sum %u): no entry on this disc (%s): "
            "carry-over: %s, journey %s, origin config\n",
            port + 1, slot, (unsigned)sum, ico_region_cur()->slug, mode_shown(d.mode), id);
    ico_difficulty_set_active(&d);
    if (f.damaged) {
        fprintf(stderr, "difficulty: the carry-over is not written into the damaged file: the "
                        "next save writes it\n");
    } else {
        char what[96];

        entry_write(&f, port, slot, &d, sum, NULL);
        snprintf(what, sizeof(what), "card %d slot %d: the carry-over", port + 1, slot);
        file_commit(&f, what);
    }
    file_close(&f);
}

/* the entry at (p, s) has this preview and an ended journey */
static int preview_finished(const File *f, int p, int s, const char *preview)
{
    const char *v = entry_get(f, p, s, "preview");

    return v != NULL && strcmp(v, preview) == 0 &&
           journey_finished(f, entry_get(f, p, s, "journey"));
}

int ico_difficulty_slot_finished(int port, int slot, int stage, uint32_t play, int sofa)
{
    File f;
    char preview[48];
    int hit_port = -1, hit_slot = -1;

    if (!slot_ok(port, slot)) {
        return 0;
    }
    if (file_open(&f) != 0) {
        file_close(&f);
        return 0;
    }
    preview_text(stage, play, sofa, preview, sizeof(preview));
    if (preview_finished(&f, port, slot, preview)) {
        hit_port = port;
        hit_slot = slot;
    }
    /* a file moved to another slot or card keeps its preview */
    for (int p = 0; hit_port < 0 && p < PORT_MAX; p++) {
        for (int s = 0; s < SLOT_MAX; s++) {
            if ((p != port || s != slot) && preview_finished(&f, p, s, preview)) {
                hit_port = p;
                hit_slot = s;
                break;
            }
        }
    }
    if (hit_port >= 0) {
        const char *id = entry_get(&f, hit_port, hit_slot, "journey");

        fprintf(stderr,
                "difficulty: the Continue list refuses card %d slot %d (preview %s): journey "
                "%s has ended (Permadeath; entry card %d slot %d)\n",
                port + 1, slot, preview, id != NULL ? id : "?", hit_port + 1, hit_slot);
    }
    file_close(&f);
    return hit_port >= 0;
}

int ico_difficulty_sum_finished(uint32_t sum)
{
    File f;
    int hit = 0;

    if (file_open(&f) != 0) {
        file_close(&f);
        return 0;
    }
    for (int p = 0; !hit && p < PORT_MAX; p++) {
        for (int s = 0; s < SLOT_MAX; s++) {
            const char *id;

            if (entry_sum(&f, p, s) != (long long)sum) {
                continue;
            }
            id = entry_get(&f, p, s, "journey");
            if (journey_finished(&f, id)) {
                fprintf(stderr,
                        "difficulty: the load refuses the save with sum %u: journey %s has "
                        "ended (Permadeath; entry card %d slot %d): back to the file list\n",
                        (unsigned)sum, id, p + 1, s);
                hit = 1;
                break;
            }
        }
    }
    file_close(&f);
    return hit;
}

void ico_difficulty_file_journey_ended(void)
{
    File f;
    char id[20], key[40], what[64];

    ico_difficulty_journey_text(ico_difficulty_active(), id, sizeof(id));
    if (!ico_difficulty_has_journey()) {
        fprintf(stderr, "difficulty: a journey ended without an id: nothing to write\n");
        return;
    }
    if (file_open(&f) != 0) {
        file_close(&f);
        fprintf(stderr, "difficulty: journey %s ended but " FILE_NAME " cannot be read\n", id);
        return;
    }
    snprintf(key, sizeof(key), "finished.%s", id);
    ico_toml_set_bool(f.t, key, 1);
    snprintf(what, sizeof(what), "journey %s ended", id);
    file_commit(&f, what);
    file_close(&f);
}

void ico_difficulty_file_knocks_reset(void)
{
    const IcoDifficulty *d = ico_difficulty_active();
    File f;
    uint32_t j[2];
    char key[96], what[96];

    if (!d->run_has_slot || !slot_ok(d->run_port, d->run_slot)) {
        fprintf(stderr, "difficulty: the journey's knock-down count is 0 again; no save of this "
                        "run yet\n");
        return;
    }
    if (file_open(&f) != 0) {
        file_close(&f);
        return;
    }
    if (f.damaged || journey_parse(entry_get(&f, d->run_port, d->run_slot, "journey"), j) != 0 ||
        j[0] != d->journey[0] || j[1] != d->journey[1]) {
        fprintf(stderr,
                "difficulty: the journey's knock-down count is 0 again; card %d slot %d holds "
                "another journey's entry or none: the next save writes it\n",
                d->run_port + 1, d->run_slot);
        file_close(&f);
        return;
    }
    entry_key(d->run_port, d->run_slot, "knocks", key, sizeof(key));
    ico_toml_set_int(f.t, key, 0);
    snprintf(what, sizeof(what), "card %d slot %d: the journey's knock-downs back to 0",
             d->run_port + 1, d->run_slot);
    file_commit(&f, what);
    file_close(&f);
}
