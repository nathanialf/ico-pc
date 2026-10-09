/*
 * port/ui/gallery.c
 *
 * The music gallery's list (gallery.h): built from the
 * game's tables, each entry named by its asset, and the page's calls into
 * the engine (gallery_play.c in the game build).
 */
#include "gallery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strings.h"

/* the streams: 1..100 music and scenes, 101..104 Yorda's hint voices */
#define LAST_MUSIC_STREAM 100
#define FIRST_VOICE_STREAM 101
#define LAST_VOICE_STREAM 104
/* stageData rows of the game itself (1 logo, 2 title, 3..39 the stages);
   40..56 are the cutscene stages, which load the same banks */
#define FIRST_GAME_STAGE 1
#define LAST_GAME_STAGE 39
#define LAST_SHIPPED_STAGE 56
/* seFile: 1..6 the common banks (COMMON.DF), 5 the voices */
#define FIRST_COMMON_BANK 1
#define LAST_COMMON_BANK 6
#define VOICE_BANK 5
#define ADPCM_PREFIX "sound/ICO_ADPCM/"
#define SE_PREFIX "sound/ICO_SE/"

static GalleryItem *s_items;
static int s_count, s_cap;
static GalleryTables s_tables;
static int s_haveTables;
static int s_built;
static const GalleryEngine *s_engine;
/* the item paused by Cross (in the engine), or held (an effect stopped by
   Cross, played again by the next); -1 */
static int s_paused = -1;
static int s_held;

void gallery_SetEngine(const GalleryEngine *e)
{
    s_engine = e;
    s_built = 0; /* the list from this engine's tables at the next enter */
}

const GalleryTables *gallery_Tables(void)
{
    return s_haveTables ? &s_tables : NULL;
}

static int add(int group, int kind, int key, int bank, int stage, int env)
{
    if (s_count == s_cap) {
        int cap = s_cap ? s_cap * 2 : 1024;
        GalleryItem *p = realloc(s_items, (size_t)cap * sizeof(*p));
        if (p == NULL) {
            return -1;
        }
        s_items = p;
        s_cap = cap;
    }
    GalleryItem *it = &s_items[s_count++];
    it->group = (unsigned char)group;
    it->kind = (unsigned char)kind;
    it->key = key;
    it->bank = (short)bank;
    it->stage = (short)stage;
    it->env = (short)env;
    return s_count - 1;
}

static void heading(int group, int bank)
{
    add(group, GAL_K_HEADING, -1, bank, -1, -1);
}

static int streamListed(const GalleryTables *t, int no)
{
    if (no <= 0 || no >= t->adpcmCount || t->adpcm[no].channels <= 0) {
        return 0;
    }
    return t->streamOnDisc == NULL || t->streamOnDisc(no);
}

/* by the disc's folders: event2/ holds the machinery's stingers (the
   scripts' handles name them: gondola, gate_open, hane1up, chain10r, ...),
   event/ and battle.int the score; the title theme (event2/50.int) is
   op.c's titleAdpcm, the title's music */
static int isSceneStream(const GalleryTables *t, int no)
{
    if (no == GALLERY_TITLE_THEME) {
        return 0;
    }
    return strstr(t->adpcm[no].path, "/event2/") != NULL;
}

/* --- banks ------------------------------------------------------------------- */

/* the first seFile row with the same .hd file as bank b */
static int bankCanon(const GalleryTables *t, int b)
{
    for (int i = 1; i < b; i++) {
        if (strcmp(t->seFile[i].hdPath, t->seFile[b].hdPath) == 0) {
            return i;
        }
    }
    return b;
}

/* whether bank b is loaded by the shipped game: a common bank, or in the
   seSeg range of a stage the game or its cutscenes load */
static int bankShipped(const GalleryTables *t, int b)
{
    if (b >= FIRST_COMMON_BANK && b <= LAST_COMMON_BANK) {
        return 1;
    }
    for (int s = FIRST_GAME_STAGE; s <= LAST_SHIPPED_STAGE && s < t->stageCount; s++) {
        if (b >= t->stage[s].seSegFirst && b < t->stage[s].seSegLast) {
            return 1;
        }
    }
    return 0;
}

/* the seDef row that plays seKind index idx: the first row of that kind */
static int defOfKind(const GalleryTables *t, int idx)
{
    for (int d = 1; d < t->seDefCount; d++) {
        if (t->seDef[d].kind == idx) {
            return d;
        }
    }
    return -1;
}

/* the seList row of kind idx in bank b, -1 */
static int listRow(const GalleryTables *t, int b, int idx)
{
    for (int j = 0; j < t->seListCount; j++) {
        if (t->seList[j].num == b && t->seList[j].idx == idx) {
            return j;
        }
    }
    return -1;
}

/* whether row j of seList plays from its bank (its header has the row's
   program and tone; unknown counts as yes) */
static int rowPlays(const GalleryTables *t, int j)
{
    return t->seInBank == NULL ||
           t->seInBank(t->seList[j].num, t->seList[j].prog, t->seList[j].tone) != 0;
}

/* the bank a def plays from on stage s: the stage's own banks first (they
   are loaded after the common ones, so soundSeKindBuild leaves theirs in
   seKind), then the common banks; -1 */
static int bankForDef(const GalleryTables *t, int def, int s)
{
    int idx = t->seDef[def].kind;
    if (s >= 0) {
        for (int b = t->stage[s].seSegLast - 1; b >= t->stage[s].seSegFirst; b--) {
            int j = b > 0 && b < t->seFileCount ? listRow(t, b, idx) : -1;
            if (j >= 0 && rowPlays(t, j)) {
                return b;
            }
        }
    }
    for (int b = LAST_COMMON_BANK; b >= FIRST_COMMON_BANK; b--) {
        int j = listRow(t, b, idx);
        if (j >= 0 && rowPlays(t, j)) {
            return b;
        }
    }
    return -1;
}

/* The effects of a bank file (every row sharing canon's .hd): one item per
   kind, in seList order, each with the first row that has it. */
static void addBankItems(const GalleryTables *t, int group, int canon)
{
    int first = s_count;
    for (int b = canon; b < t->seFileCount; b++) {
        if (bankCanon(t, b) != canon || !bankShipped(t, b)) {
            continue;
        }
        for (int j = 0; j < t->seListCount; j++) {
            if (t->seList[j].num != b) {
                continue;
            }
            int def = defOfKind(t, t->seList[j].idx);
            if (def < 0 || listRow(t, b, t->seList[j].idx) != j || !rowPlays(t, j)) {
                continue; /* the engine plays a kind's first row in the bank */
            }
            int dup = 0;
            for (int k = first; k < s_count && !dup; k++) {
                dup = s_items[k].key == def;
            }
            if (!dup) {
                add(group, GAL_K_SE, def, b, -1, -1);
            }
        }
    }
}

static int bankHasItems(const GalleryTables *t, int canon)
{
    for (int b = canon; b < t->seFileCount; b++) {
        if (bankCanon(t, b) != canon || !bankShipped(t, b)) {
            continue;
        }
        for (int j = 0; j < t->seListCount; j++) {
            if (t->seList[j].num == b && defOfKind(t, t->seList[j].idx) >= 0 &&
                listRow(t, b, t->seList[j].idx) == j && rowPlays(t, j)) {
                return 1;
            }
        }
    }
    return 0;
}

int gallery_Build(const GalleryTables *t)
{
    s_count = 0;
    s_built = 1;
    if (t != &s_tables) {
        if (t != NULL) {
            s_tables = *t;
        } else {
            memset(&s_tables, 0, sizeof(s_tables));
        }
    }
    s_haveTables = t != NULL && t->adpcm != NULL;
    if (t == NULL || t->adpcm == NULL) {
        add(GAL_G_BACK, GAL_K_BACK, -1, -1, -1, -1);
        return s_count;
    }
    /* Soundtrack, then Scene sounds */
    for (int pass = 0; pass < 2; pass++) {
        int group = pass == 0 ? GAL_G_SOUNDTRACK : GAL_G_SCENE;
        heading(group, -1);
        for (int no = 1; no <= LAST_MUSIC_STREAM; no++) {
            if (streamListed(t, no) && isSceneStream(t, no) == pass) {
                add(group, GAL_K_STREAM, no, -1, -1, -1);
            }
        }
    }
    /* Ambience: each stage's environment sounds, once each */
    heading(GAL_G_AMBIENCE, -1);
    int ambFirst = s_count;
    for (int s = FIRST_GAME_STAGE; s <= LAST_GAME_STAGE && s < t->stageCount; s++) {
        for (int e = t->stage[s].seEnvFirst; e < t->stage[s].seEnvLast && e < t->seEnvCount; e++) {
            int def = t->seEnv[e].se;
            if (def <= 0 || def >= t->seDefCount) {
                continue;
            }
            int dup = 0;
            for (int k = ambFirst; k < s_count && !dup; k++) {
                dup = s_items[k].key == def;
            }
            int bank = bankForDef(t, def, s);
            if (!dup && bank > 0) {
                add(GAL_G_AMBIENCE, GAL_K_SE, def, bank, s, e);
            }
        }
    }
    /* Voice: the hint voices, then the com_v bank */
    heading(GAL_G_VOICE, -1);
    for (int no = FIRST_VOICE_STREAM; no <= LAST_VOICE_STREAM; no++) {
        if (streamListed(t, no)) {
            add(GAL_G_VOICE, GAL_K_STREAM, no, -1, -1, -1);
        }
    }
    if (VOICE_BANK < t->seFileCount) {
        addBankItems(t, GAL_G_VOICE, bankCanon(t, VOICE_BANK));
    }
    /* Sound effects, bank by bank */
    for (int b = 1; b < t->seFileCount; b++) {
        if (b == VOICE_BANK || bankCanon(t, b) != b || !bankShipped(t, b) || !bankHasItems(t, b)) {
            continue;
        }
        heading(GAL_G_SE, b);
        addBankItems(t, GAL_G_SE, b);
    }
    add(GAL_G_BACK, GAL_K_BACK, -1, -1, -1, -1);
    return s_count;
}

int gallery_Count(void)
{
    return s_count;
}

const GalleryItem *gallery_Item(int i)
{
    return i >= 0 && i < s_count ? &s_items[i] : NULL;
}

int gallery_Find(int group, int kind, int key, int bank)
{
    for (int i = 0; i < s_count; i++) {
        const GalleryItem *it = &s_items[i];
        if ((group < 0 || it->group == group) && it->kind == kind && it->key == key &&
            (bank < 0 || it->bank == bank)) {
            return i;
        }
    }
    return -1;
}

int gallery_JumpGroup(int i, int dir)
{
    /* the group starts: every heading, and Back */
    int own = -1, starts[GAL_G_COUNT * 128], n = 0;
    for (int k = 0; k < s_count && n < (int)(sizeof(starts) / sizeof(starts[0])); k++) {
        int head =
            s_items[k].kind == GAL_K_HEADING && (k == 0 || s_items[k - 1].kind != GAL_K_HEADING);
        if (head || s_items[k].kind == GAL_K_BACK) {
            if (k <= i) {
                own = n;
            }
            starts[n++] = k;
        }
    }
    if (n == 0) {
        return -1;
    }
    if (own < 0) {
        own = n - 1;
    }
    int k = starts[((own + dir) % n + n) % n];
    while (k < s_count && s_items[k].kind == GAL_K_HEADING) {
        k++;
    }
    return k < s_count ? k : -1;
}

/* --- texts ------------------------------------------------------------------ */

static const char *baseName(const char *p)
{
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static const char *streamAsset(int no)
{
    const char *p = s_tables.adpcm[no].path;
    return strncmp(p, ADPCM_PREFIX, strlen(ADPCM_PREFIX)) == 0 ? p + strlen(ADPCM_PREFIX) : p;
}

/* a stream's name: its file without the folder and the .int */
static const char *streamName(int no, char *buf, size_t n)
{
    snprintf(buf, n, "%s", streamAsset(no));
    char *dot = strrchr(buf, '.');
    if (dot && strchr(dot, '/') == NULL) {
        *dot = '\0';
    }
    return buf;
}

/* a bank's name: its .hd file without the folder and the extension */
static const char *bankName(int b, char *buf, size_t n)
{
    snprintf(buf, n, "%s", baseName(s_tables.seFile[b].hdPath));
    char *dot = strrchr(buf, '.');
    if (dot) {
        *dot = '\0';
    }
    return buf;
}

int gallery_GroupStr(int group)
{
    static const int ids[GAL_G_COUNT] = {UI_STR_GAL_SOUNDTRACK, UI_STR_GAL_SCENE,
                                         UI_STR_GAL_AMBIENCE,   UI_STR_GAL_VOICE,
                                         UI_STR_GAL_SE,         UI_STR_BACK};
    return group >= 0 && group < GAL_G_COUNT ? ids[group] : UI_STR_NONE;
}

const char *gallery_Label(int i, char *buf, size_t n)
{
    const GalleryItem *it = gallery_Item(i);
    if (it == NULL) {
        return "";
    }
    switch (it->kind) {
    case GAL_K_HEADING:
        if (it->group == GAL_G_SE && it->bank > 0) {
            char b[64];
            snprintf(buf, n, "%s: %s", ui_Str((UiStrId)UI_STR_GAL_SE),
                     bankName(it->bank, b, sizeof(b)));
            return buf;
        }
        return ui_Str((UiStrId)gallery_GroupStr(it->group));
    case GAL_K_STREAM:
        return streamName(it->key, buf, n);
    case GAL_K_SE:
        snprintf(buf, n, "%.32s", s_tables.seDef[it->key].name);
        if (buf[0] == '\0') {
            snprintf(buf, n, "se %d", it->key);
        }
        return buf;
    case GAL_K_BACK:
        return ui_Str(UI_STR_BACK);
    default:
        return "";
    }
}

const char *gallery_ColA(int i, char *buf, size_t n)
{
    const GalleryItem *it = gallery_Item(i);
    (void)buf;
    (void)n;
    if (it == NULL) {
        return "";
    }
    if (it->group == GAL_G_AMBIENCE && it->stage >= 0) {
        return s_tables.stage[it->stage].key;
    }
    return "";
}

const char *gallery_Asset(int i, char *buf, size_t n)
{
    const GalleryItem *it = gallery_Item(i);
    if (it == NULL) {
        return "";
    }
    if (it->kind == GAL_K_STREAM) {
        return streamAsset(it->key);
    }
    if (it->kind == GAL_K_SE || (it->kind == GAL_K_HEADING && it->bank > 0)) {
        const char *hd = s_tables.seFile[it->bank].hdPath;
        if (strncmp(hd, SE_PREFIX, strlen(SE_PREFIX)) == 0) {
            hd += strlen(SE_PREFIX);
        }
        if (it->kind == GAL_K_HEADING) {
            snprintf(buf, n, "%s", hd);
        } else {
            int row = listRow(&s_tables, it->bank, s_tables.seDef[it->key].kind);
            snprintf(buf, n, "%s %d/%d", hd, row >= 0 ? s_tables.seList[row].prog : -1,
                     row >= 0 ? s_tables.seList[row].tone : -1);
        }
        return buf;
    }
    return "";
}

/* --- a stream's time ------------------------------------------------------- */

double gallery_StreamSeconds(const AdpcmDataRec *r, double bytes)
{
    int ch = r->channels > 0 ? r->channels : 1;
    int hz = r->pitch > 0 ? r->pitch : 44100;
    return bytes / ch / 16.0 * 28.0 / hz;
}

double gallery_StreamBytes(const AdpcmDataRec *r)
{
    return (double)r->sectors * 2048.0;
}

void gallery_ClockReset(GalleryStreamClock *c)
{
    memset(c, 0, sizeof(*c));
}

unsigned long long gallery_ClockStep(GalleryStreamClock *c, uint32_t nax, uint32_t start,
                                     uint32_t size)
{
    if (size == 0 || nax < start || nax >= start + size) {
        return c->played;
    }
    if (!c->started) {
        c->started = 1;
        c->prev = start;
    }
    c->step = (nax - c->prev + size) % size;
    if (c->step > size / 2) {
        /* NAX went back: the voice is looping a block (a blank sector's
           end flags) or was keyed again; nothing played */
        c->step = 0;
    }
    c->played += c->step;
    c->prev = nax;
    return c->played;
}

int gallery_ClockAtEnd(const GalleryStreamClock *c, int channels, double bytes)
{
    int ch = channels > 0 ? channels : 1;
    return c->started && ((double)c->played + c->step / 2.0) * ch >= bytes;
}

long gallery_StreamEndBlock(const uint8_t *buf, size_t n, int channels)
{
    for (size_t sec = 0; sec + GALLERY_SECTOR <= n; sec += GALLERY_SECTOR) {
        for (size_t b = 0; b < GALLERY_SECTOR; b += 16) {
            if (buf[sec + b + 1] & 1) {
                return (long)sec;
            }
        }
    }
    (void)channels; /* every channel's chunk is blocks of 16 bytes */
    return -1;
}

long long gallery_StreamBlankFrom(uint64_t pass,
                                  int (*read)(void *user, uint64_t off, uint8_t *buf), void *user)
{
    uint8_t buf[GALLERY_SECTOR];
    const long long n = (long long)(pass / GALLERY_SECTOR);
    if (n <= 0 || read(user, (uint64_t)(n - 1) * GALLERY_SECTOR, buf) != 0 ||
        gallery_StreamEndBlock(buf, GALLERY_SECTOR, 1) < 0) {
        return -1; /* the pass's last sector plays: no blank tail */
    }
    /* sectors below lo play, hi and every one after it are blank */
    long long lo = -1, hi = n - 1;
    while (hi - lo > 1) {
        const long long mid = lo + (hi - lo) / 2;
        if (read(user, (uint64_t)mid * GALLERY_SECTOR, buf) != 0) {
            return -1;
        }
        if (gallery_StreamEndBlock(buf, GALLERY_SECTOR, 1) >= 0) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return hi * GALLERY_SECTOR;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static unsigned rd16(const uint8_t *p)
{
    return (unsigned)p[0] | (unsigned)p[1] << 8;
}

int gallery_HdHas(const uint8_t *hd, size_t size, int prog, int tone)
{
    if (hd == NULL || size < 0x24 || prog < 0 || prog >= 0x80 || tone < 0 || tone >= 0x80 ||
        rd32(hd + 0x0C) != 0x64685353u || rd32(hd + 0x20) == 0xFFFFFFFFu) {
        return 0;
    }
    uint32_t off = rd32(hd + 0x1C);
    if (off == 0xFFFFFFFFu || (size_t)off + 2 > size) {
        return 0;
    }
    const uint8_t *tbl = hd + off;
    if (rd16(tbl) < (unsigned)prog || off + 2 * (size_t)(prog + 2) > size) {
        return 0;
    }
    unsigned pe = rd16(tbl + 2 * (prog + 1));
    if (pe == 0xFFFF || off + (size_t)(pe / 2) * 2 + 2 > size) {
        return 0;
    }
    return rd16(tbl + (pe / 2) * 2) >= (unsigned)tone;
}

/* --- the page --------------------------------------------------------------- */

/* the ICO_GALLERY_PLAY script: entries kind:value, comma separated, each
   played SCRIPT_TICKS Main ticks from SCRIPT_START ticks after the page
   opens; then "gallery: script done".  After a dwell:S entry each entry
   instead lasts until it has ended (a second after its end line) or S
   seconds (it is then stopped: "cut"), and the position is logged every
   second; dwell:0 goes back to the fixed steps.  The sweep
   (test/gallery_sweep.py) plays every stream and effects of every bank so. */
#define SCRIPT_MAX 512
#define SCRIPT_START 50
#define SCRIPT_TICKS 200
extern int systemStatus[12]; /* [0]: 1 PAL 50 Hz, 0 60 Hz; [1]: the frame step */

/* Main ticks a second in the video mode in force, as ico_gs_tick_hz */
static int ticksPerSecond(void)
{
    const int step = systemStatus[1] > 0 ? systemStatus[1] : 2;
    return (systemStatus[0] ? 50 : 60) / step;
}

static char s_scriptKind[SCRIPT_MAX][8];
static int s_scriptVal[SCRIPT_MAX], s_scriptVal2[SCRIPT_MAX];
static int s_scriptN, s_scriptPos, s_scriptTick, s_scriptDone, s_scriptLeave;
static int s_dwell;     /* seconds an entry may last; 0: SCRIPT_TICKS steps */
static int s_dwellItem; /* the entry playing in dwell mode, -1 */
static int s_dwellTick, s_dwellSeen, s_dwellGone;
/* the item sounding at the last tick (-1) and where it was, for the end
   and wrap lines (gallery_Tick) */
static int s_track = -1;
static float s_trackEl, s_trackTot;

static const char *groupToken(int g)
{
    static const char *const t[GAL_G_COUNT] = {"soundtrack", "scene", "ambience",
                                               "voice",      "se",    "back"};
    return g >= 0 && g < GAL_G_COUNT ? t[g] : "?";
}

static void scriptParse(void)
{
    const char *env = getenv("ICO_GALLERY_PLAY");
    s_scriptN = s_scriptPos = s_scriptTick = s_scriptDone = s_scriptLeave = 0;
    s_dwell = 0;
    s_dwellItem = -1;
    if (env == NULL || env[0] == '\0') {
        return;
    }
    char *buf = malloc(strlen(env) + 1);
    if (buf == NULL) {
        return;
    }
    strcpy(buf, env);
    for (char *tok = strtok(buf, ",|"); tok && s_scriptN < SCRIPT_MAX; tok = strtok(NULL, ",|")) {
        char *colon = strchr(tok, ':');
        if (colon == NULL) {
            fprintf(stderr, "gallery: failed script entry \"%s\" (kind:value)\n", tok);
            continue;
        }
        *colon = '\0';
        snprintf(s_scriptKind[s_scriptN], sizeof(s_scriptKind[0]), "%s", tok);
        const char *dot = strchr(colon + 1, '.');
        s_scriptVal2[s_scriptN] = dot ? atoi(dot + 1) : 0;
        s_scriptVal[s_scriptN++] = atoi(colon + 1);
    }
    free(buf);
    fprintf(stderr, "gallery: script of %d entries\n", s_scriptN);
}

/* the K-th bank's section of the sound effects: its first (J 0), middle
   (1) or last (2) effect, -1 */
static int bankItem(int k, int j)
{
    int head = -1, n = 0;
    for (int i = 0; i < s_count; i++) {
        if (s_items[i].group == GAL_G_SE && s_items[i].kind == GAL_K_HEADING && n++ == k) {
            head = i;
            break;
        }
    }
    if (head < 0) {
        return -1;
    }
    int last = head;
    while (last + 1 < s_count && s_items[last + 1].kind == GAL_K_SE &&
           s_items[last + 1].group == GAL_G_SE) {
        last++;
    }
    if (last == head) {
        return -1;
    }
    return j <= 0 ? head + 1 : j == 1 ? head + 1 + (last - head - 1) / 2 : last;
}

/* the item a script entry names, -1 */
static int scriptItem(const char *kind, int v, int v2)
{
    if (strcmp(kind, "stream") == 0) {
        return gallery_Find(-1, GAL_K_STREAM, v, -1);
    }
    if (strcmp(kind, "env") == 0) {
        for (int i = 0; i < s_count; i++) {
            if (s_items[i].group == GAL_G_AMBIENCE && s_items[i].kind == GAL_K_SE && s_haveTables &&
                v >= 0 && v < s_tables.seEnvCount && s_items[i].key == s_tables.seEnv[v].se) {
                return i;
            }
        }
        return -1;
    }
    if (strcmp(kind, "se") == 0) {
        int i = gallery_Find(GAL_G_VOICE, GAL_K_SE, v, -1);
        return i >= 0 ? i : gallery_Find(GAL_G_SE, GAL_K_SE, v, -1);
    }
    if (strcmp(kind, "bank") == 0) {
        return bankItem(v, v2);
    }
    return -1;
}

static const char *kindToken(const GalleryItem *it)
{
    return it->kind == GAL_K_STREAM ? "stream" : "effect";
}

/* dwell mode: whether the entry playing is over (ended, cut, or never
   sounded) */
static int dwellOver(void)
{
    if (s_dwellItem < 0) {
        return 1;
    }
    const GalleryItem *it = &s_items[s_dwellItem];
    s_dwellTick++;
    if (gallery_Playing() == s_dwellItem) {
        s_dwellSeen = 1;
        s_dwellGone = 0;
    } else if (s_dwellSeen) {
        s_dwellGone++;
    }
    if (s_dwellSeen && s_dwellGone >= ticksPerSecond()) {
        return 1;
    }
    if (!s_dwellSeen && s_dwellTick >= 15 * ticksPerSecond()) {
        fprintf(stderr, "gallery: failed %s %d: never sounded\n", kindToken(it), it->key);
        return 1;
    }
    if (s_dwellTick >= s_dwell * ticksPerSecond()) {
        float el = 0.0f, tot = 0.0f;
        gallery_Position(&el, &tot);
        fprintf(stderr, "gallery: %s %d cut at %.1f s of %.1f s (dwell %d s)\n", kindToken(it),
                it->key, el, tot, s_dwell);
        gallery_Stop();
        return 1;
    }
    return 0;
}

static void scriptStep(void)
{
    if (s_scriptN == 0 || s_scriptDone) {
        return;
    }
    s_scriptTick++;
    if (s_scriptTick < SCRIPT_START) {
        return;
    }
    if (s_dwell > 0) {
        if (!dwellOver()) {
            return;
        }
        s_dwellItem = -1;
    } else {
        int t = s_scriptTick - SCRIPT_START;
        if (t % SCRIPT_TICKS != 0) {
            return;
        }
    }
    if (s_scriptPos >= s_scriptN) {
        gallery_Stop();
        s_scriptDone = 1;
        fprintf(stderr, "gallery: script done\n");
        return;
    }
    const char *kind = s_scriptKind[s_scriptPos];
    int v = s_scriptVal[s_scriptPos], v2 = s_scriptVal2[s_scriptPos];
    s_scriptPos++;
    if (strcmp(kind, "dwell") == 0) {
        s_dwell = v > 0 ? v : 0;
        s_scriptTick = SCRIPT_START; /* the next entry: now, or one step on */
        fprintf(stderr, "gallery: dwell %d s\n", s_dwell);
        return;
    }
    if (strcmp(kind, "leave") == 0) {
        s_scriptLeave = 1; /* the page leaves, as Triangle */
        s_scriptDone = 1;
        fprintf(stderr, "gallery: script done\n");
        return;
    }
    if (strcmp(kind, "pause") == 0) {
        /* Cross on the item sounding or paused */
        int i = gallery_Playing();
        if (i < 0) {
            fprintf(stderr, "gallery: failed pause: nothing plays\n");
            return;
        }
        gallery_Toggle(i);
        return;
    }
    if (strcmp(kind, "seq") == 0) {
        fprintf(stderr, "gallery: failed seq %d: the disc has no sequenced music\n", v);
        return;
    }
    int i = scriptItem(kind, v, v2);
    if (i < 0) {
        fprintf(stderr, "gallery: failed %s %d: no such entry in the list\n", kind, v);
        return;
    }
    if (gallery_Play(i) != 0) {
        return; /* logged; the next entry at the next step */
    }
    if (s_dwell > 0) {
        s_dwellItem = i;
        s_dwellTick = s_dwellSeen = s_dwellGone = 0;
    }
}

int gallery_ScriptLeave(void)
{
    int r = s_scriptLeave;
    s_scriptLeave = 0;
    return r;
}

void gallery_Enter(void)
{
    if (!s_built) {
        memset(&s_tables, 0, sizeof(s_tables));
        s_haveTables = s_engine && s_engine->tables && s_engine->tables(&s_tables) == 0;
        gallery_Build(s_haveTables ? &s_tables : NULL);
        int n[GAL_G_COUNT] = {0};
        for (int i = 0; i < s_count; i++) {
            if (s_items[i].kind != GAL_K_HEADING) {
                n[s_items[i].group]++;
            }
        }
        fprintf(stderr,
                "gallery: %d entries: soundtrack %d, scene sounds %d, ambience %d, voice %d, "
                "sound effects %d\n",
                s_count, n[GAL_G_SOUNDTRACK], n[GAL_G_SCENE], n[GAL_G_AMBIENCE], n[GAL_G_VOICE],
                n[GAL_G_SE]);
    }
    if (s_engine && s_engine->enter) {
        s_engine->enter();
    }
    scriptParse();
}

void gallery_Leave(void)
{
    s_track = -1;
    s_paused = -1;
    s_held = 0;
    if (s_engine && s_engine->leave) {
        s_engine->leave();
    }
}

int gallery_Play(int i)
{
    const GalleryItem *it = gallery_Item(i);
    if (it == NULL || (it->kind != GAL_K_STREAM && it->kind != GAL_K_SE)) {
        return -1;
    }
    s_paused = -1;
    s_held = 0;
    s_track = -1;
    char buf[96];
    fprintf(stderr, "gallery: playing %s %d (%s)\n", groupToken(it->group), it->key,
            gallery_Label(i, buf, sizeof(buf)));
    if (s_engine == NULL || s_engine->play == NULL) {
        return -1;
    }
    return s_engine->play(it);
}

void gallery_Stop(void)
{
    s_paused = -1;
    s_held = 0;
    s_track = -1;
    if (s_engine && s_engine->stop) {
        s_engine->stop();
    }
}

/* once every LOG_TICKS Main ticks while an item sounds (every second in
   the script's dwell mode): where it is; when it ends by itself, where it
   ended; when its position goes back, that it wrapped */
#define LOG_TICKS 50
static int s_logTick;

void gallery_Tick(void)
{
    if (s_engine && s_engine->tick) {
        s_engine->tick();
    }
    float el = 0.0f, tot = 0.0f;
    int i = gallery_Playing();
    int pos = i >= 0 && gallery_Position(&el, &tot) == 0;
    if (s_track >= 0 && i != s_track) {
        fprintf(stderr, "gallery: %s %d ended at %.1f s of %.1f s\n", kindToken(&s_items[s_track]),
                s_items[s_track].key, s_trackEl, s_trackTot);
        s_track = -1;
    }
    if (pos) {
        if (s_track == i && el + 1.0f < s_trackEl) {
            fprintf(stderr, "gallery: %s %d wrapped at %.1f s to %.1f s of %.1f s\n",
                    kindToken(&s_items[i]), s_items[i].key, s_trackEl, el, tot);
        }
        s_track = i;
        s_trackEl = el;
        s_trackTot = tot;
        if (++s_logTick >= (s_dwell > 0 ? ticksPerSecond() : LOG_TICKS)) {
            s_logTick = 0;
            fprintf(stderr, "gallery: %s %d at %.1f s of %.1f s%s\n", kindToken(&s_items[i]),
                    s_items[i].key, el, tot, s_paused == i ? " (paused)" : "");
        }
    } else {
        s_logTick = 0;
    }
    if (s_paused >= 0 && !s_held && gallery_Playing() != s_paused) {
        s_paused = -1; /* it ended or was stopped under the page */
    }
    scriptStep();
}

int gallery_Playing(void)
{
    const GalleryItem *it = s_engine && s_engine->playing ? s_engine->playing() : NULL;
    return it != NULL && it >= s_items && it < s_items + s_count ? (int)(it - s_items) : -1;
}

int gallery_Paused(void)
{
    return s_paused;
}

int gallery_Toggle(int i)
{
    const GalleryItem *it = gallery_Item(i);
    if (it == NULL || (it->kind != GAL_K_STREAM && it->kind != GAL_K_SE)) {
        return -1;
    }
    if (s_paused == i && !s_held) {
        if (s_engine && s_engine->pause && s_engine->pause(0) == 0) {
            s_paused = -1;
            fprintf(stderr, "gallery: resumed %s %d\n", groupToken(it->group), it->key);
            return 0;
        }
    }
    if (s_paused == i && s_held) {
        return gallery_Play(i); /* from its start */
    }
    if (gallery_Playing() == i) {
        if (s_engine && s_engine->pause && s_engine->pause(1) == 0) {
            s_paused = i;
            s_held = 0;
            fprintf(stderr, "gallery: paused %s %d\n", groupToken(it->group), it->key);
        } else {
            /* the engine cannot pause it: stopped, and Cross plays it again */
            gallery_Stop();
            s_paused = i;
            s_held = 1;
            fprintf(stderr, "gallery: stopped %s %d (held)\n", groupToken(it->group), it->key);
        }
        return 0;
    }
    return gallery_Play(i);
}

int gallery_Position(float *elapsed, float *total)
{
    *elapsed = *total = 0.0f;
    if (gallery_Playing() < 0 || s_engine == NULL || s_engine->position == NULL) {
        return -1;
    }
    return s_engine->position(elapsed, total);
}

int gallery_Step(int i, int dir)
{
    if (s_count == 0) {
        return -1;
    }
    dir = dir < 0 ? -1 : 1;
    int k = i < 0 ? (dir > 0 ? -1 : 0) : i;
    for (int guard = 0; guard < s_count; guard++) {
        k = ((k + dir) % s_count + s_count) % s_count;
        if (s_items[k].kind == GAL_K_STREAM || s_items[k].kind == GAL_K_SE) {
            return k;
        }
    }
    return -1;
}
