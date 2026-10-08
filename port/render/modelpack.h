/* modelpack.h: model packs (v0.4.1, M3): replacement models for the game's
 * mesh parts, and the dump that writes those parts out for pack makers.
 *
 * Identity: a part is named by its mesh hash (rd_mesh.h rd_VuMeshDescHash,
 * XXH3-64 over the part's tagless VU stream), written as 16 lower-case hex
 * digits.  The files (gltf.h has the layout and the reader's rules):
 *
 *   folders   <user folder>/models/SCES-50760/replacements, then
 *             <program folder>/models/SCES-50760/replacements, then the
 *             tolerant layouts (models/replacements, files directly under
 *             models/) of both, as texture packs (texpack.h); walked
 *             recursively, ".gltf" and ".glb" in any case; the first file
 *             of a hash wins, later ones are counted as duplicates; other
 *             files (the .bin beside a .gltf, readmes, pictures) are passed
 *             over quietly.
 *   names     <hash16>.gltf / .glb, or <anything>-<hash16>.gltf / .glb (the
 *             last 16 characters before the extension, lower-case hex);
 *             any other .gltf / .glb is counted as "not a model name".
 *   dumps     <user folder>/models/SCES-50760/dumps/<hash16>.gltf + .bin,
 *             and one line per part in dumps/models.txt (hash, model, part,
 *             ordinal, layout, vertices, batches, bones).
 *
 * Loading: modelpack_Init reads and converts every file at once (no
 * thread): each primitive (a triangle list) becomes VU strips, primitive i
 * drawing in the place of the original's batch i.  The conversion:
 *   strips    greedy: a triangle joins the strip when it shares the strip's
 *             last edge (either orientation; VU meshes draw without
 *             culling, so winding is free), else a new strip starts (ST.w
 *             0 on its first vertex, 1 elsewhere).  Vertices equal in
 *             every converted byte are merged first, triangles that use a
 *             vertex twice dropped.
 *   ST        (u, v, 1, flag) from TEXCOORD_0 ((0, 0) without it)
 *   colour    round(c * 255) per channel as the raw GS byte (128 is normal
 *             brightness, extras.ico.colorScale), alpha 127; (128, 128,
 *             128) without COLOR_0
 *   normals   normalised; normal.w is the original part's (set when the
 *             replacement is made)
 *   bones     the two largest weights of JOINTS_0 / WEIGHTS_0, renormalised
 *             to sum 1 (one bone: (bone, 1) and (bone 0, 0) as the game
 *             writes it), each joint as the VU address bone * 4 + 16;
 *             a joint of 60 or more refuses the file
 *   static    the mesh node's world transform applied to positions and
 *             normals; skinned meshes keep their bind-pose positions
 *   layout    skinned (5 quadwords a vertex) when a primitive has JOINTS_0,
 *             lit (4) when every primitive has NORMAL, prelit (3) else.  A
 *             lit file serves a prelit part (the normals are dropped:
 *             Blender always exports them); any other difference from the
 *             original declines the file for that part.
 * Caps: 512 MB of converted meshes in all, 1,048,576 vertices and 4096
 * batches (primitives) per mesh.
 *
 * Log lines (player-facing): "models: N replacements from <folder>",
 * "models: no model pack (looked in ...)", "models: <file> skipped:
 * <reason>", "models: <file> not used for <part>: <reason>".
 *
 * Every function runs on the game fiber.
 */
#ifndef PORT_RENDER_MODELPACK_H
#define PORT_RENDER_MODELPACK_H

#include <stdbool.h>
#include <stdint.h>

#include "rd_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MODELPACK_MAX_BYTES (512ull << 20)
#define MODELPACK_MAX_VERTICES 1048576u
#define MODELPACK_MAX_BATCHES 4096u
#define MODELPACK_MAX_BONES 60u /* VU memory 16..255: Packet.c's "no < 60" */

typedef struct ModelpackConfig {
    const char *userDir;    /* the user folder (ico_pref_dir), or NULL: no dumps */
    const char *programDir; /* the program's folder, or NULL */
    const char *serial;     /* "SCES-50760" (NULL: that) */
    int developer;          /* gameplay.developer_mode: a log line per replacement made */
    int dumpEnabled;        /* video.dump_models: dump every part drawn (developer mode) */
} ModelpackConfig;

typedef struct ModelpackStats {
    uint32_t files;      /* .gltf / .glb files found */
    uint32_t indexed;    /* replacements ready (modelpack_Count) */
    uint32_t duplicates; /* a later file of a hash already indexed */
    uint32_t badNames;   /* a .gltf / .glb whose name is not a model name */
    uint32_t failed;     /* files that could not be read or converted (logged) */
    uint32_t declined;   /* entries declined for a part (modelpack_Create, modelpack_Decline) */
    uint32_t created;    /* replacement meshes made */
    uint32_t dumped;     /* parts written by the dump */
    uint64_t bytes;      /* RAM the converted meshes hold */
} ModelpackStats;

/* Walks the folders, reads and converts every model file, logs what it
 * found.  Replaces a previous Init (modelpack_Shutdown first).  The pack
 * starts enabled.  Returns the number of replacements (modelpack_Count). */
int modelpack_Init(const ModelpackConfig *c);
/* Replacements ready (0 before Init or without a pack: the Settings row
 * then says "None installed"). */
int modelpack_Count(void);
void modelpack_GetStats(ModelpackStats *out);
/* Frees the index and the converted meshes; the replacement meshes already
 * made stay as they are (rd owns them). */
void modelpack_Shutdown(void);

/* The entry of a hash, or -1: no pack, switched off (modelpack_SetEnabled),
 * no file of that hash, or declined. */
int modelpack_Lookup(uint64_t hash);

/* A mesh that draws entry's model in the place of the part orig describes
 * (rd_CreateVuMeshReplacement; name names the record, NULL: orig's).
 * boneCount: the object's skeleton node count (Sub15C.nodeNum) for a
 * skinned part, ignored otherwise.  Checked first: orig hashes to the
 * entry's hash, the layouts agree (a lit file serves a prelit part), the
 * file has no more primitives than orig has batches, and every bone it uses
 * is below min(boneCount, 60).  On a mismatch the entry is declined (one log
 * line) and {0} returned: the caller builds the original (rd_CreateVuMesh). */
RdMesh modelpack_Create(int entry, const RdVuMeshDesc *orig, const char *name, uint32_t boneCount);
/* Declines a hash's entry from now on (modelpack_Lookup returns -1), e.g.
 * for a part that changes shape every frame (the morph path cannot write a
 * replaced mesh).  The caller logs why. */
void modelpack_Decline(uint64_t hash);

/* The Options switch: off retires every replaced mesh (rd_VuMeshRetire, so
 * the game builds its originals again on the next draw) and makes Lookup
 * return -1; on retires every original whose hash has an entry (so the
 * replacements are made on the next draw). */
void modelpack_SetEnabled(bool on);
bool modelpack_Enabled(void);

/* ---------------------------------------------------------------- dumps */

typedef struct ModelpackIdent {
    const char *model;  /* the model's name (regKeyGrp->name) */
    int part;           /* the part's index (regKeyIdx) */
    int ordinal;        /* the packet's ordinal in the part (regKeyOrdinal) */
    const void *obj;    /* the drawing object (regKeyObj), for
                          modelpack_DumpObjectOnce; may be NULL */
    uint64_t buildHash; /* the hash the mesh was built with (what
                           modelpack_DumpWanted was asked); 0 if unknown.  A part
                           that changes shape hashes differently every frame:
                           this one is marked done too */
} ModelpackIdent;

/* A skinned part's skeleton: bone i = skeleton node i (Sub15C.skel),
 * invBind[i] = clusterMtx[i] verbatim (gltf.h: a PS2 matrix is its glTF
 * matrix), parent[i] the parent node (< 0 for a root). */
typedef struct ModelpackSkeleton {
    uint32_t count; /* nodeNum, at most 60 */
    float invBind[60][16];
    int parent[60];
} ModelpackSkeleton;

/* The game's skeleton of a skinned part whose entry was just used: logged
 * once per file if the file's own inverse bind matrices differ (they are
 * ignored; the game's are used). */
void modelpack_NoteSkeleton(int entry, const ModelpackSkeleton *skel);

/* Whether the draw of a part (its mesh hash, the drawing object) should
 * be dumped now: with dumping on, a hash not dumped yet this session; and
 * every part of the object modelpack_DumpObjectOnce armed, in the frame it
 * is first drawn.  Cheap: call it at every mesh draw and build the desc for
 * modelpack_Dump only when it says so. */
bool modelpack_DumpWanted(uint64_t hash, const void *obj);
/* Whether anything wants a mesh's hash now: a pack with files, dumping on,
 * or a shot armed. */
bool modelpack_HashWanted(void);
/* Writes the part orig describes (skel: NULL for a static part) as
 * dumps/<hash16>.gltf + .bin and appends its line to dumps/models.txt.
 * Once per hash a session, and a file already on disk is kept, except for
 * the armed object (id->obj), whose parts are written again.  The number
 * of files written (2, or 0). */
int modelpack_Dump(const RdVuMeshDesc *orig, const ModelpackIdent *id,
                   const ModelpackSkeleton *skel);
/* Developer mode's dump switch at run time (Init takes the first value). */
void modelpack_SetDumpEnabled(bool on);
/* Arms a one-shot dump of every part drawn for obj (the model viewer's
 * "Save this model's files"), dumping on or off: modelpack_DumpWanted says
 * yes for obj's parts in the first frame that draws obj.  NULL disarms. */
void modelpack_DumpObjectOnce(const void *obj);
/* The shot's outcome: -1 while it is armed and its frame not finished (or
 * obj not drawn yet), else the files the last shot wrote (0 when none was
 * armed). */
int modelpack_DumpObjectStatus(void);
/* The dumps folder (empty before the first dump made it). */
const char *modelpack_DumpDir(void);

#ifdef __cplusplus
}
#endif

#endif
