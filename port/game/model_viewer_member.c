/*
 * port/game/model_viewer_member.c
 *
 * Extras > Models' member rows (model_viewer.h MvModel.member): the early
 * show title art, three textured quads of the E3 2001 title that every
 * retail disc's logo pack carries (MV_MEMBER_PACK) and only the E3 title
 * script, whose pack no retail disc has, draws.  The game loads them with
 * the logo pack at every boot and drops them with the next stage, so the
 * viewer registers them again on its host stage, once that stage is up
 * (model_viewer.c setup, from Main's tick):
 *
 *   - the model member and the TIM2 member it names are read from the disc
 *     into host memory (port/data/df_pack.h ico_df_find_member and
 *     ico_df_read_member, the program's DATA.DF index on the game thread);
 *   - each is handed to ico_char_file_image (charFileManager.h), which does
 *     what the pack loader does with a member (fumi/ios/cdvd.c
 *     iosCdvdMgrPackLoad): its extension's initFunc row, the loader's body
 *     on the image (ReadTextureFile's: the texture table; ReadModelFile's:
 *     charFiles[id] and the next objSerial), the load count, with the
 *     member's own directory words and a stage pack's segment (1), the
 *     texture first as the pack holds it (its textures before its models:
 *     the model's material finds its texture by name when its object is
 *     built, Packet.c pac_getTextureInfo);
 *   - the object is built as a stage builds a layouted object
 *     (sceneManager.c initSceneGObj: CreateLayoutedGObj): kind 35 (MOBJ,
 *     the kind of the stage animation objects the E3 title drew them
 *     with: objTableScene; its row has no constructor and draws with
 *     p2o_DispVU1MultiDefault, the static objects' reg_DispObj), at the
 *     origin with stage_MakeGObj's setting (no turn, scale 1), the light 0
 *     and the display group 7 of most of the stages' static objects and of
 *     every stage animation object, no layout row (label -1, the engine's
 *     own "none": CreateGObjByFuncSet), no accessary (-1, as
 *     CSVSYSTEM_InitDObj leaves it).  model_viewer.c then finds it by its
 *     kind and model as any other row.
 *
 * Memory: the game's arena, as the pack's load puts them: the stage
 * partition (ios_partition_seki, reset at the next stage change, as the
 * pack's own members are) and malloc partition 1; the model's charFiles
 * entry and the texture's table slot are the stage's (state and partition
 * 1), cleared by the next stage change as the logo pack's own (charFileManager.c
 * ResetCharFileManager, Texture.c tex_Init).  Nothing is freed by the
 * viewer: left registered as the pack loader leaves its members, for the
 * stage's life.  Each pick of a member row loads the host stage afresh
 * (model_viewer.c mvStart), so the members are registered once per visit
 * of the stage.  The host copies are freed at once.
 *
 * The loaders stop the game on a failed allocation or a full table, so
 * the room is checked first and the load refused with a line when it is
 * short: the stage partition (its free list read, as yorda_trip_live.c
 * does: the members' bytes with their nodes, 1 MB kept free, the largest
 * block holding the largest member), the texture table (Texture.c asserts
 * at 200 entries), the object pool (obj_manager.c isysGObjInit's count).
 *
 * Log: `unused: title art <member> (id N from STGLOG.DF) loaded for the
 * viewer (...)`, or `unused: title art <member> not loaded for the viewer:
 * <why>`.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "typedef.h"
#include "gobj.h"
#include "ios.h"
#include "memory.h"
#include "charFileManager.h"
#include "sceneManager.h"
#include "Texture.h"

#include "../core/region.h" /* ico_region_cur */
#include "df_pack.h"
#include "diag_host.h"
#include "model_viewer.h"
#include "vfs.h"

extern int stage_no;

/* a stage pack's segment (icoMisc.c InitIcoMisc loads the stage's pack
   with 1, the logo pack among them) */
#define ART_SEG 1
/* the display group and the light of the stages' static objects and of
   stage_MakeGObj's objects */
#define ART_KEY 7
#define ART_LIGHT 0
/* no layout row, no accessary */
#define ART_LABEL (-1)
#define ART_ACCESSARY (-1)
/* the stage partition keeps at least this much free after them (the
   host's 4 MB over the PS2's, ios.c ICO_HOST_STAGE_EXTRA, as Yorda's
   trips keep: yorda_trip_live.c) */
#define ART_RESERVE (1024 * 1024)
/* Texture.c tex_initTextureSub asserts once its table holds 200 */
#define TEX_TABLE 200
/* obj_manager.c: isysGObjInit's object pool */
#define GOBJ_POOL PAL_JUN26(320, 288)
/* a member larger than this is not one of these quads */
#define ART_MAX_BYTES (4 * 1024 * 1024)

/* the free bytes of a partition from its free list, read only (as
   yorda_trip_live.c freeBytes) */
static long freeBytes(const IosMemPart *part, long *largest)
{
    long sum = 0;
    int n = 0;

    *largest = 0;
    for (const IosMemNode *node = part != NULL ? part->head : NULL; node != NULL && n < 1 << 20;
         node = node->free_next, n++) {
        if (strncmp(node->tag, "<FREE AREA>", 11) != 0) {
            break;
        }
        const long bytes = (long)node->size << 4;
        sum += bytes;
        *largest = bytes > *largest ? bytes : *largest;
    }
    return sum;
}

static const char *baseName(const char *path)
{
    const char *b = strrchr(path, '/');
    return b != NULL ? b + 1 : path;
}

/* the texture table's name of a member: its file name to the first '.'
   (Texture.c texTrimName) */
static void textureName(const char *member, char *out, size_t cap)
{
    snprintf(out, cap, "%.63s", baseName(member));
    char *dot = strchr(out, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
}

static int liveObjects(void)
{
    int n = 0;
    for (GObj *g = isysGObjGetExist_begin(); g != NULL && n < 1 << 16;
         g = isysGObjGetExist_next(g)) {
        n++;
    }
    return n;
}

static GObj *builtAlready(const MvModel *m, int id)
{
    for (GObj *g = isysGObjGetExist_begin(); g != NULL; g = isysGObjGetExist_next(g)) {
        if (g->kind == m->kind && g->dobj != NULL && g->dobj->modelId == id) {
            return g;
        }
    }
    return NULL;
}

static GObj *refuse(const MvModel *m, const char *why)
{
    fprintf(stderr, "unused: title art %.60s not loaded for the viewer: %.200s (stage %d)\n",
            baseName(m->member), why, stage_no);
    return NULL;
}

/* where member `name` is, checked to be the logo pack's; 0, or -1 with why */
static int findMember(IcoVfs *vfs, const char *name, IcoDfMember *out, char *why, size_t cap)
{
    const char *pack;

    if (ico_df_find_member(vfs, name, out) != 0) {
        snprintf(why, cap, "no pack of this disc holds %.120s", name);
        return -1;
    }
    pack = ico_df_entry_name(vfs, out->pack);
    if (pack == NULL || strcmp(pack, MV_MEMBER_PACK) != 0) {
        snprintf(why, cap, "%.100s is in %.40s, not %s", name, pack != NULL ? pack : "?",
                 MV_MEMBER_PACK);
        return -1;
    }
    if (out->size == 0 || out->size > ART_MAX_BYTES) {
        snprintf(why, cap, "%.100s has %u bytes", name, (unsigned)out->size);
        return -1;
    }
    return 0;
}

static const char *imageRefusal(int rc)
{
    switch (rc) {
    case ICO_CHAR_IMAGE_NO_LOADER:
        return "the pack loader has no row for its extension";
    case ICO_CHAR_IMAGE_OTHER_LOADER:
        return "its extension's loader is not the model or texture loader";
    case ICO_CHAR_IMAGE_BAD_ID:
        return "its model id is outside the model table";
    case ICO_CHAR_IMAGE_BAD_SIZE:
        return "it is empty";
    default:
        return "refused";
    }
}

struct GObj *mv_member_load(const MvModel *m)
{
    char why[256];
    char texName[64];
    char modelMember[160];
    char texMember[160];
    IcoDfMember dmModel, dmTex;
    IcoVfs *vfs;
    const int id = mv_model_id(m);
    GObj *g;

    if (m == NULL || m->member == NULL || m->texture == NULL) {
        return NULL;
    }
    if (id < 0) {
        return refuse(m, "this disc's logo pack does not hold it");
    }
    g = builtAlready(m, id);
    if (g != NULL) {
        fprintf(stderr, "unused: title art %.60s (id %d from %s) already on this stage\n",
                baseName(m->member), id, MV_MEMBER_PACK);
        return g;
    }
    vfs = ico_vfs_disc();
    if (vfs == NULL) {
        return refuse(m, "the disc cannot be read");
    }
    if (findMember(vfs, m->member, &dmModel, why, sizeof why) != 0 ||
        findMember(vfs, m->texture, &dmTex, why, sizeof why) != 0) {
        return refuse(m, why);
    }
    if (dmModel.id != id) {
        snprintf(why, sizeof why, "its id changed from %d to %d", id, dmModel.id);
        return refuse(m, why);
    }

    /* the room the loaders take for granted */
    textureName(m->texture, texName, sizeof texName);
    const int texHad = tex_GetTextureNo(texName) >= 0;
    if (!texHad && tex_GetTextureNum() + 1 >= TEX_TABLE) {
        snprintf(why, sizeof why, "the texture table is full (%d of %d)", tex_GetTextureNum(),
                 TEX_TABLE);
        return refuse(m, why);
    }
    const int live = liveObjects();
    if (live + 1 >= GOBJ_POOL) {
        snprintf(why, sizeof why, "the object pool is full (%d of %d)", live, GOBJ_POOL);
        return refuse(m, why);
    }
    long largest = 0;
    const long left = freeBytes(ios_partition_seki, &largest);
    const long node = (long)sizeof(IosMemNode);
    const long need = (long)((dmModel.size + 15u) & ~15u) + (long)((dmTex.size + 15u) & ~15u) +
                      2 * node + 64 * 1024; /* its model and object records */
    const long biggest = (long)(dmModel.size > dmTex.size ? dmModel.size : dmTex.size);
    if (left - need < ART_RESERVE || largest < biggest + node) {
        snprintf(why, sizeof why,
                 "the stage's memory is short (%ld KB free, largest block %ld KB, %ld KB needed "
                 "with %d KB kept free)",
                 left / 1024, largest / 1024, need / 1024, ART_RESERVE / 1024);
        return refuse(m, why);
    }

    /* the members, read */
    const unsigned long long t0 = ico_diag_now_ns();
    void *texBytes = malloc(dmTex.size);
    void *modelBytes = malloc(dmModel.size);
    if (texBytes == NULL || modelBytes == NULL) {
        free(texBytes);
        free(modelBytes);
        return refuse(m, "out of host memory");
    }
    if (ico_df_read_member(vfs, &dmTex, texBytes) != 0 ||
        ico_df_read_member(vfs, &dmModel, modelBytes) != 0) {
        free(texBytes);
        free(modelBytes);
        return refuse(m, "the disc read failed");
    }
    const double readMs = (double)(ico_diag_now_ns() - t0) / 1e6;

    /* registered as the pack registers them: the texture, then the model;
       the loaders' name is the member's directory name, a writable copy;
       the directory's word08 is passed as 0: no loader reads it (cdvd.c
       PackEnt; neither ReadModelFile nor ReadTextureFile names it) and
       df_pack.h's index does not keep it */
    snprintf(texMember, sizeof texMember, "%.159s", m->texture);
    snprintf(modelMember, sizeof modelMember, "%.159s", m->member);
    const int rcTex =
        ico_char_file_image(texMember, texBytes, (int)dmTex.size, dmTex.id, dmTex.kind, 0, ART_SEG);
    const int rcModel = rcTex >= 0 ? ico_char_file_image(modelMember, modelBytes, (int)dmModel.size,
                                                         dmModel.id, dmModel.kind, 0, ART_SEG)
                                   : rcTex;
    free(texBytes);
    free(modelBytes);
    if (rcTex < 0) {
        snprintf(why, sizeof why, "its texture %.60s: %s", baseName(m->texture),
                 imageRefusal(rcTex));
        return refuse(m, why);
    }
    if (tex_GetTextureNo(texName) < 0) {
        snprintf(why, sizeof why, "its texture %.60s is not in the texture table", texName);
        return refuse(m, why);
    }
    if (rcModel < 0) {
        snprintf(why, sizeof why, "its model: %s (the texture %.60s stays for the stage)",
                 imageRefusal(rcModel), texName);
        return refuse(m, why);
    }
    if (GetPObjAddress(id) == NULL) {
        return refuse(m, "its model is not in the model table");
    }

    /* the object, as a stage builds a layouted object of the kind */
    SObjSimpleSetting at = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, 0};
    g = CreateLayoutedGObj(m->kind, id, ART_ACCESSARY, ART_LIGHT, &at, ART_LABEL, ART_KEY, 0);
    if (g == NULL || g->dobj == NULL || g->dobj->model == NULL) {
        return refuse(m, "its object was not built");
    }
    fprintf(stderr,
            "unused: title art %.60s (id %d from %s) loaded for the viewer (texture %.60s%s, %u "
            "KB read in %.0f ms%s; stage %d, %ld KB of its memory left)\n",
            baseName(m->member), id, MV_MEMBER_PACK, texName, texHad ? ", there already" : "",
            (unsigned)((dmModel.size + dmTex.size + 1023u) / 1024u), readMs,
            rcModel == ICO_CHAR_IMAGE_ALREADY ? ", the model there already" : "", stage_no,
            freeBytes(ios_partition_seki, &largest) / 1024);
    return g;
}
