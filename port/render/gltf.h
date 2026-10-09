/*
 * port/render/gltf.h
 *
 * glTF 2.0 for model packs: the dump writes a model part as
 * <path>.gltf + <path>.bin, the pack reads a replacement back from a .gltf
 * (its buffers in files next to it) or a .glb.  CPU only, no device and no
 * game state; JSON through port/data/json.h.  Library ico_gltf (gltf.c on
 * ico_json and ico_host_fs, UTF-8 paths).
 *
 * The in-memory form (GltfDoc) is neutral: one mesh, its primitives as
 * triangle lists, one optional skin, the mesh node's world transform and
 * the asset's extras as JSON text.  modelpack.c turns it into and
 * out of the game's batches.
 *
 * Matrices: 16 floats in glTF's order, column-major (element row r,
 * column c at m[c * 4 + r]), applied to column vectors (p' = M p).  The
 * PS2's matrices are row-vector (p' = p M) and stored row-major, which is
 * the same 16 floats: a PS2 matrix IS its glTF matrix verbatim, never
 * transposed.  So inverseBindMatrices[i] = clusterMtx[i] as the game holds
 * it, and a PS2 product A * B (row-vector: A first) is gltf_Mat4Mul(B, A).
 *
 * What gltf_Write writes (the dump's layout, the contract with pack
 * makers):
 *   asset       version "2.0", generator "ico-pc", extras = extrasText
 *               verbatim (omitted when NULL)
 *   scene 0     node 0 (the mesh node) and the root joints
 *   node 0      name and mesh 0 = meshName, matrix = nodeMatrix (omitted
 *               when it is the identity), skin 0 when skin.count > 0
 *   nodes 1..n  the joints: name "bone_00", "bone_01", ... (bone i is node
 *               1 + i and skin joint i), matrix = inverse(parent's bind) *
 *               inverse(invBind[i]) (the bind pose's local transform;
 *               inverse(invBind[i]) for a root; omitted when the
 *               identity, the identity again when singular), children
 *               from parent[]
 *   skins[0]    joints = nodes 1..n in bone order, inverseBindMatrices =
 *               invBind verbatim (MAT4 float)
 *   meshes[0]   primitives[i] = prims[i], mode 4 (TRIANGLES), attributes
 *               POSITION (float VEC3, with min/max), NORMAL (float VEC3),
 *               TEXCOORD_0 (float VEC2), COLOR_0 (float VEC3), JOINTS_0
 *               (ubyte VEC4), WEIGHTS_0 (float VEC4), each when present;
 *               indices uint32 when idx is set; extras {"name": name}
 *               when name is set
 *   buffer 0    <basename>.bin, little-endian: one bufferView per kind
 *               (positions, normals, uvs, colours, joints, weights,
 *               indices, inverse binds), the primitives' data in order
 *   numbers     floats as "%.9g" (exact on read), whatever the locale
 *
 * What gltf_Read accepts:
 *   containers  a .gltf (JSON) whose buffers have a relative uri, read
 *               from the .gltf's folder (percent escapes decoded; no
 *               "data:" or other schemes, no absolute paths, no "..");
 *               a .glb (12-byte header, a JSON chunk, an optional BIN
 *               chunk that is buffer 0 when it has no uri).  Told apart
 *               by the "glTF" magic, not the extension.
 *   the mesh    the first node with a mesh in a depth-first walk of the
 *               scene (asset "scene", else scene 0; with no scenes, every
 *               node without a parent); nodeMatrix = its world transform
 *               (each node's matrix, or translation * rotation * scale).
 *               Other mesh nodes are ignored.  No mesh node: meshes[0]
 *               with the identity.
 *   primitives  mode 4 only, at most GLTF_MAX_PRIMS; POSITION required;
 *               all attributes of a primitive the same count; the counts
 *               summed over the mesh at most GLTF_MAX_VERTICES, the index
 *               counts at most GLTF_MAX_INDICES; indices a multiple of 3
 *               and each below the vertex count (no indices: the vertex
 *               count a multiple of 3).
 *   accessors   POSITION float VEC3; NORMAL float VEC3; TEXCOORD_0 VEC2
 *               float or normalized ubyte/ushort; COLOR_0 VEC3/VEC4 float
 *               or normalized ubyte/ushort (alpha dropped); JOINTS_0 VEC4
 *               ubyte/ushort; WEIGHTS_0 VEC4 float or normalized
 *               ubyte/ushort; indices SCALAR ubyte/ushort/uint;
 *               inverseBindMatrices MAT4 float.  byteStride honoured.
 *               Every accessor bounds-checked against its bufferView and
 *               the bufferView against its buffer and the bytes actually
 *               there.  Other attributes (TANGENT, TEXCOORD_1, JOINTS_1,
 *               morph targets...) are ignored.
 *   the skin    the mesh node's skin.  Joints map to bone numbers by node
 *               NAME "bone_NN" (decimal, below GLTF_MAX_JOINTS) when every
 *               joint is named so, by skin order when none is; mixed or
 *               duplicate names are refused.  Out: skin.count = highest
 *               bone + 1, invBind[bone] verbatim (identity when the skin
 *               has none), parent[bone] = the bone of the joint's parent
 *               node (-1 when that is not a joint), names[bone]; a bone
 *               number no joint has gets the identity, parent -1, name "".
 *               JOINTS_0 values come out as bone numbers.  JOINTS_0 and
 *               WEIGHTS_0 come together and need a skin.
 *   refused     with a one-line reason in why: data: URIs, sparse
 *               accessors, other modes, a missing POSITION, the caps,
 *               accessor types outside the list, short buffers or files,
 *               an extensionsRequired entry, asset.version not 2.x,
 *               malformed JSON or GLB.
 *
 * Ownership: gltf_Read allocates every array, string and prim; gltf_Free
 * releases them (only for a document gltf_Read filled, after success or
 * failure: a failed read leaves *out empty).  gltf_Write only reads the
 * caller's document; the caller owns it.
 */
#ifndef PORT_RENDER_GLTF_H
#define PORT_RENDER_GLTF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GLTF_MAX_PRIMS 4096u
#define GLTF_MAX_VERTICES 1048576u
#define GLTF_MAX_INDICES (3u * GLTF_MAX_VERTICES)
#define GLTF_MAX_JOINTS 256u                         /* JOINTS_0 is ubyte in GltfPrim */
#define GLTF_MAX_FILE_BYTES (256u * 1024u * 1024u)   /* one .gltf, .glb or .bin */
#define GLTF_MAX_BUFFERS 16                          /* buffers one model file lists */
#define GLTF_MAX_BUFFER_BYTES (256u * 1024u * 1024u) /* all of a model file's buffers together */

/* One primitive, a triangle list.  Per-vertex arrays have vertexCount
 * entries of the stated width; any but pos may be NULL (absent).  idx: a
 * triangle list of indexCount entries (a multiple of 3), NULL for
 * non-indexed (consecutive vertices, vertexCount a multiple of 3).
 * joints are bone numbers (GltfSkin's index), weights their weights. */
typedef struct GltfPrim {
    float *pos;      /* 3 per vertex */
    float *nrm;      /* 3, NULL when absent */
    float *uv;       /* 2 (TEXCOORD_0), NULL when absent */
    float *col;      /* 3 (COLOR_0 rgb), NULL when absent */
    uint8_t *joints; /* 4, NULL when absent (with weights) */
    float *weights;  /* 4, NULL when absent (with joints) */
    uint32_t *idx;   /* indexCount, NULL for non-indexed */
    uint32_t vertexCount;
    uint32_t indexCount;
    const char *name; /* the primitive's extras.name, NULL when none */
} GltfPrim;

/* The skeleton, indexed by bone number.  count 0: no skin. */
typedef struct GltfSkin {
    uint32_t count;       /* at most GLTF_MAX_JOINTS */
    float (*invBind)[16]; /* inverse bind matrices (column-major, see above) */
    int *parent;          /* parent bone, < 0 for a root */
    char (*names)[16];    /* read: the joint node's name (truncated to 15
                             bytes); write: ignored, joints are named
                             bone_NN; may be NULL on write */
} GltfSkin;

typedef struct GltfDoc {
    GltfPrim *prims;
    uint32_t primCount;   /* 1..GLTF_MAX_PRIMS */
    const char *meshName; /* meshes[0].name and the mesh node's; NULL: none */
    GltfSkin skin;
    char *extrasText;     /* asset.extras as JSON text: write, a JSON
                               object written verbatim (NULL: none); read,
                               the value re-serialized compactly (NULL when
                               absent) */
    float nodeMatrix[16]; /* the mesh node's world transform; on write
                               all zeros counts as the identity */
} GltfDoc;

/* Zeroes *doc with nodeMatrix the identity. */
void gltf_DocInit(GltfDoc *doc);

/* Writes <path>.gltf and <path>.bin (path without extension; its folder
 * must exist).  0 on success; -1 with a reason in why (invalid document,
 * non-finite numbers, I/O) and neither file left behind. */
int gltf_Write(const char *path, const GltfDoc *doc, char *why, size_t whyLen);

/* Reads a .gltf or .glb file into *out (initialised first).  0 on success;
 * -1 with a reason in why and *out empty. */
int gltf_Read(const char *path, GltfDoc *out, char *why, size_t whyLen);

/* Frees what gltf_Read allocated and re-initialises *doc; NULL is fine. */
void gltf_Free(GltfDoc *doc);

/* 4x4 helpers in the convention above (column-major, column vectors).
 * out may alias an input. */
void gltf_Mat4Identity(float out[16]);
void gltf_Mat4Mul(float out[16], const float a[16], const float b[16]); /* a * b */
/* out = inverse(m) (computed in double); -1 and out the identity when m is
 * singular or not finite. */
int gltf_Mat4Invert(float out[16], const float m[16]);

#ifdef __cplusplus
}
#endif

#endif
