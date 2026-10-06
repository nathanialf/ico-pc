/*
 * port/ui/gallery.c
 *
 * The music gallery's list (gallery.h; docs/port/MUSIC.md): built from the
 * game's tables, each entry named by its asset, and the page's calls into
 * the engine (gallery_play.c in the game build).
 */
#include "gallery.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "strings.h"

/* op.c: the title theme, actTitleShortCut's request (kind 56, titleAdpcm) */
#define TITLE_THEME 56
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
    if (no == TITLE_THEME) {
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

/* the bank a def plays from on stage s: the stage's own banks first (they
   are loaded after the common ones, so soundSeKindBuild leaves theirs in
   seKind), then the common banks; -1 */
static int bankForDef(const GalleryTables *t, int def, int s)
{
    int idx = t->seDef[def].kind;
    if (s >= 0) {
        for (int b = t->stage[s].seSegLast - 1; b >= t->stage[s].seSegFirst; b--) {
            if (b > 0 && b < t->seFileCount && listRow(t, b, idx) >= 0) {
                return b;
            }
        }
    }
    for (int b = LAST_COMMON_BANK; b >= FIRST_COMMON_BANK; b--) {
        if (listRow(t, b, idx) >= 0) {
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
            if (def < 0) {
                continue;
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
            if (t->seList[j].num == b && defOfKind(t, t->seList[j].idx) >= 0) {
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

/* --- the page --------------------------------------------------------------- */

/* the ICO_GALLERY_PLAY script: entries kind:value, comma separated, each
   played SCRIPT_TICKS Main ticks from SCRIPT_START ticks after the page
   opens; then "gallery: script done" */
#define SCRIPT_MAX 16
#define SCRIPT_START 50
#define SCRIPT_TICKS 200
static char s_scriptKind[SCRIPT_MAX][8];
static int s_scriptVal[SCRIPT_MAX];
static int s_scriptN, s_scriptPos, s_scriptTick, s_scriptDone;

static const char *groupToken(int g)
{
    static const char *const t[GAL_G_COUNT] = {"soundtrack", "scene", "ambience",
                                               "voice",      "se",    "back"};
    return g >= 0 && g < GAL_G_COUNT ? t[g] : "?";
}

static void scriptParse(void)
{
    const char *env = getenv("ICO_GALLERY_PLAY");
    s_scriptN = s_scriptPos = s_scriptTick = s_scriptDone = 0;
    if (env == NULL || env[0] == '\0') {
        return;
    }
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", env);
    for (char *tok = strtok(buf, ",|"); tok && s_scriptN < SCRIPT_MAX; tok = strtok(NULL, ",|")) {
        char *colon = strchr(tok, ':');
        if (colon == NULL) {
            fprintf(stderr, "gallery: failed script entry \"%s\" (kind:value)\n", tok);
            continue;
        }
        *colon = '\0';
        snprintf(s_scriptKind[s_scriptN], sizeof(s_scriptKind[0]), "%s", tok);
        s_scriptVal[s_scriptN++] = atoi(colon + 1);
    }
    fprintf(stderr, "gallery: script of %d entries\n", s_scriptN);
}

/* the item a script entry names, -1 */
static int scriptItem(const char *kind, int v)
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
    return -1;
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
    int t = s_scriptTick - SCRIPT_START;
    if (t % SCRIPT_TICKS != 0) {
        return;
    }
    if (s_scriptPos >= s_scriptN) {
        gallery_Stop();
        s_scriptDone = 1;
        fprintf(stderr, "gallery: script done\n");
        return;
    }
    const char *kind = s_scriptKind[s_scriptPos];
    int v = s_scriptVal[s_scriptPos++];
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
    int i = scriptItem(kind, v);
    if (i < 0) {
        fprintf(stderr, "gallery: failed %s %d: no such entry in the list\n", kind, v);
        return;
    }
    gallery_Play(i);
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
    if (s_engine && s_engine->stop) {
        s_engine->stop();
    }
}

/* once every LOG_TICKS Main ticks while an item sounds: where it is */
#define LOG_TICKS 50
static int s_logTick;

void gallery_Tick(void)
{
    if (s_engine && s_engine->tick) {
        s_engine->tick();
    }
    float el, tot;
    int i = gallery_Playing();
    if (i >= 0 && gallery_Position(&el, &tot) == 0) {
        if (++s_logTick >= LOG_TICKS) {
            s_logTick = 0;
            fprintf(stderr, "gallery: %s %d at %.1f s of %.1f s%s\n",
                    s_items[i].kind == GAL_K_STREAM ? "stream" : "effect", s_items[i].key, el, tot,
                    s_paused == i ? " (paused)" : "");
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
