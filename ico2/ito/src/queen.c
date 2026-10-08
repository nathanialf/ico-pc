#include "typedef.h"
#include "queen.h"
#include "debug.h"
#include "memory.h"
#include "pad.h"
#include "gobj.h"
#include "obj_manager.h"
#include "act.h"
#include "enemy_act.h"
#include "queen_barrier_disp.h"
#include "attackhit.h"
#include "generator.h"
#include "script.h"
#include "StageAnimation.h"
#include "actressLight.h"
#include "clothAnimation.h"
#include "darkVolume.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "lodManager.h"
#include "matrixDrive.h"
#include "motionManager2.h"
#include "particleEffect.h"
#include "quaternion.h"
#include <libvu0.h>
#include <math.h>
#include "weapon.h"
#include "motionOrientManager.h"
#include "DisplayP2O.h"
#include "ios.h"
#include <string.h>
#include "main.h"
#include "Matrix.h"
#include "itou_common.h"
#include "itou_sub.h"
#include "gather_effect.h"
#include "boyact.h"
#include "Texture.h"

typedef struct { /* field names derived */
    float v[4];
} LVec; /* derived name */

typedef struct { /* field names derived */
    QVec x;
    QVec y;
    QVec z;
    QVec w;
} QMat44; /* derived name */

typedef struct { /* field names derived */
    QVec x;
    QVec y;
    QVec z;
} QMat3; /* derived name */

/* A node matrix written as a 3x3 and a translation row: CopyMatrix copies all
   64 bytes, so the translation has to sit right after the rotation. */
typedef struct { /* field names derived */
    QMat3 rot;
    QVec trans;
} QMat3T; /* derived name */

/* The work records the queen's three objects hang at their work word.  The
   queen's (InitQueenGeo): four flag bytes (paused by mail 0x2E/0x2F,
   attacking, hit by the sword this frame, dead), the boy's weapon power the
   gathered souls raise, the two cloths. */
typedef struct QueenWork { /* field names derived */

    union {
        int all; /* the four flags read as one word */

        struct {
            signed char pause;   /* 0x00 */
            signed char attack;  /* 0x01 */
            signed char damaged; /* 0x02 */
            signed char dead;    /* 0x03 */
        } f;
    } st;

    int power;       /* 0x04 */
    int wait;        /* 0x08, frames before the power counts again */
    int clothOn;     /* 0x0C */
    Cloth4D *cloth;  /* 0x10, the cloths InitCloth4D returns */
    Cloth4D *cloth2; /* 0x14 */
} QueenWork;         /* derived name */

/* the barrier's (object kind 54, InitQueenBarrierGeo) */
typedef struct QueenBarrierWork { /* field names derived */
    float pos[4];                 /* 0x00 */
    signed char hit;              /* 0x10, struck this frame */
    signed char react;            /* 0x11, the queen's reaction motion is pending */
    signed char active;           /* 0x12 */
    char pad13[1];
    float radius; /* 0x14 */
    int damage;   /* 0x18, hits taken, broken at 5 */
    char pad1C[4];
    float rot[4];   /* 0x20 */
} QueenBarrierWork; /* derived name */

/* the ball's (object kind 53, InitQueenBallGeo) */
typedef struct QueenBallWork { /* field names derived */
    float pos[4];              /* 0x00 */
    signed char busy;          /* 0x10, thrown and not yet over */
    signed char live;          /* 0x11, flying */
    char pad12[2];
    float scale;          /* 0x14 */
    signed char attacked; /* 0x18, struck by the sword */
    signed char hit;      /* 0x19, has hit the boy */
    signed char cancel;   /* 0x1A */
    char pad1B[1];
    BgaPlayNode *bga; /* 0x1C, the ball's BG animation */
} QueenBallWork;      /* derived name */

/* the queen's four ball-ring animations */
static BgaPlayNode *queenBga[4]; /* derived name */

/* the texture both of the queen's cloth meshes are drawn with */
static const char queenClothTexture[] = "queen_effect2"; /* derived name */

/* The queen's two generated cloth meshes, in the order InitQueenGeo hands them
   to InitCloth4D: the cape, which hangs from queenClothHang, and the second
   cloth, which hangs from nothing.  The floats are the mesh generator's own
   three-decimal output. */

static Cloth4DCol queenClothMeshCols[15]; /* derived name */

static Cloth4DCfg queenClothMesh = {
    /* derived name */
    15, 11, 1, 0, 0, 0, 0, 128, queenClothTexture, queenClothMeshCols, 17.058f, 0,
};

static float queenClothMeshUv[15][11][2] = {
    /* derived name */
    {{0.839f, 0.127f},
     {0.871f, 0.207f},
     {0.904f, 0.286f},
     {0.937f, 0.366f},
     {0.97f, 0.445f},
     {1.003f, 0.525f},
     {1.035f, 0.605f},
     {1.068f, 0.684f},
     {1.101f, 0.764f},
     {1.134f, 0.844f},
     {1.166f, 0.923f}},
    {{0.624f, 0.114f},
     {0.644f, 0.193f},
     {0.663f, 0.272f},
     {0.683f, 0.351f},
     {0.702f, 0.43f},
     {0.722f, 0.509f},
     {0.741f, 0.588f},
     {0.761f, 0.668f},
     {0.78f, 0.747f},
     {0.8f, 0.826f},
     {0.819f, 0.905f}},
    {{0.973f, 0.094f},
     {0.917f, 0.174f},
     {0.86f, 0.254f},
     {0.804f, 0.334f},
     {0.748f, 0.414f},
     {0.691f, 0.493f},
     {0.635f, 0.573f},
     {0.579f, 0.653f},
     {0.523f, 0.733f},
     {0.466f, 0.813f},
     {0.41f, 0.893f}},
    {{0.771f, 0.078f},
     {0.782f, 0.158f},
     {0.794f, 0.238f},
     {0.805f, 0.318f},
     {0.817f, 0.398f},
     {0.828f, 0.477f},
     {0.84f, 0.557f},
     {0.851f, 0.637f},
     {0.863f, 0.717f},
     {0.874f, 0.797f},
     {0.886f, 0.877f}},
    {{0.499f, 0.071f},
     {0.509f, 0.151f},
     {0.519f, 0.231f},
     {0.53f, 0.311f},
     {0.54f, 0.391f},
     {0.55f, 0.472f},
     {0.561f, 0.552f},
     {0.571f, 0.632f},
     {0.581f, 0.712f},
     {0.591f, 0.792f},
     {0.602f, 0.872f}},
    {{0.37f, 0.113f},
     {0.368f, 0.19f},
     {0.367f, 0.266f},
     {0.366f, 0.343f},
     {0.364f, 0.419f},
     {0.363f, 0.495f},
     {0.361f, 0.572f},
     {0.36f, 0.648f},
     {0.359f, 0.725f},
     {0.357f, 0.801f},
     {0.356f, 0.878f}},
    {{0.497f, 0.114f},
     {0.508f, 0.191f},
     {0.518f, 0.267f},
     {0.529f, 0.343f},
     {0.539f, 0.419f},
     {0.55f, 0.496f},
     {0.56f, 0.572f},
     {0.571f, 0.648f},
     {0.581f, 0.724f},
     {0.592f, 0.801f},
     {0.602f, 0.877f}},
    {{0.023f, 0.09f},
     {0.035f, 0.176f},
     {0.046f, 0.263f},
     {0.058f, 0.35f},
     {0.069f, 0.437f},
     {0.081f, 0.523f},
     {0.092f, 0.61f},
     {0.104f, 0.697f},
     {0.116f, 0.784f},
     {0.127f, 0.87f},
     {0.139f, 0.957f}},
    {{0.206f, 0.074f},
     {0.217f, 0.145f},
     {0.227f, 0.216f},
     {0.238f, 0.287f},
     {0.248f, 0.358f},
     {0.259f, 0.429f},
     {0.269f, 0.5f},
     {0.28f, 0.571f},
     {0.29f, 0.642f},
     {0.301f, 0.714f},
     {0.311f, 0.785f}},
    {{0.964f, 0.121f},
     {0.871f, 0.198f},
     {0.778f, 0.275f},
     {0.685f, 0.352f},
     {0.592f, 0.429f},
     {0.5f, 0.506f},
     {0.407f, 0.583f},
     {0.314f, 0.66f},
     {0.221f, 0.737f},
     {0.128f, 0.814f},
     {0.035f, 0.891f}},
    {{0.03f, 0.116f},
     {-0.016f, 0.19f},
     {-0.062f, 0.264f},
     {-0.109f, 0.338f},
     {-0.155f, 0.411f},
     {-0.201f, 0.485f},
     {-0.247f, 0.559f},
     {-0.293f, 0.633f},
     {-0.339f, 0.706f},
     {-0.385f, 0.78f},
     {-0.431f, 0.854f}},
    {{0.154f, 0.123f},
     {0.201f, 0.202f},
     {0.247f, 0.281f},
     {0.293f, 0.36f},
     {0.339f, 0.439f},
     {0.385f, 0.518f},
     {0.431f, 0.597f},
     {0.477f, 0.676f},
     {0.523f, 0.754f},
     {0.569f, 0.833f},
     {0.615f, 0.912f}},
    {{0.261f, 0.113f},
     {0.249f, 0.189f},
     {0.236f, 0.265f},
     {0.224f, 0.341f},
     {0.211f, 0.418f},
     {0.199f, 0.494f},
     {0.186f, 0.57f},
     {0.174f, 0.646f},
     {0.161f, 0.723f},
     {0.149f, 0.799f},
     {0.136f, 0.875f}},
    {{0.733f, 0.115f},
     {0.758f, 0.195f},
     {0.782f, 0.276f},
     {0.807f, 0.356f},
     {0.832f, 0.436f},
     {0.857f, 0.516f},
     {0.882f, 0.597f},
     {0.907f, 0.677f},
     {0.931f, 0.757f},
     {0.956f, 0.838f},
     {0.981f, 0.918f}},
    {{0.624f, 0.114f},
     {0.644f, 0.193f},
     {0.663f, 0.272f},
     {0.683f, 0.351f},
     {0.702f, 0.43f},
     {0.722f, 0.509f},
     {0.741f, 0.588f},
     {0.761f, 0.668f},
     {0.78f, 0.747f},
     {0.8f, 0.826f},
     {0.819f, 0.905f}},
};

static Cloth4DCol queenClothMeshCols[15] = {
    /* derived name */
    {11.74f,
     {0},
     {-6.96f, 84.761f, 3.983f, 1.0f},
     {-0.163f, 0.376f, 0.912f, 0.0f},
     40,
     0.1f,
     45,
     0.9f,
     queenClothMeshUv[1],
     {0},
     {-2.007f, -10.392f, 5.08f, 0.0f}},
    {11.883f,
     {0},
     {-12.811f, 84.763f, 2.835f, 1.0f},
     {-0.41f, 0.429f, 0.805f, 0.0f},
     40,
     0.1f,
     45,
     0.9f,
     queenClothMeshUv[13],
     {0},
     {-3.776f, -10.393f, 4.352f, 0.0f}},
    {11.889f,
     {0},
     {-17.743f, 83.39f, -0.429f, 1.0f},
     {-0.788f, 0.473f, 0.395f, 0.0f},
     40,
     0.1f,
     45,
     0.9f,
     queenClothMeshUv[0],
     {0},
     {-5.722f, -10.257f, 1.841f, 0.0f}},
    {12.951f,
     {0},
     {-17.716f, 84.158f, -11.773f, 1.0f},
     {-0.87f, 0.489f, -0.068f, 0.0f},
     40,
     0.5f,
     45,
     0.5f,
     queenClothMeshUv[9],
     {0},
     {-5.83f, -10.332f, -5.194f, 0.0f}},
    {14.351f,
     {0},
     {-13.033f, 87.372f, -18.54f, 1.0f},
     {-0.602f, 0.66f, -0.449f, 0.0f},
     40,
     0.7f,
     45,
     0.3f,
     queenClothMeshUv[2],
     {0},
     {-4.245f, -10.646f, -8.637f, 0.0f}},
    {15.248f,
     {0},
     {-6.168f, 89.387f, -21.167f, 1.0f},
     {-0.367f, 0.7f, -0.612f, 0.0f},
     40,
     1.0f,
     -1,
     0.0f,
     queenClothMeshUv[3],
     {0},
     {-1.94f, -10.842f, -10.545f, 0.0f}},
    {15.087f,
     {0},
     {0.0f, 89.912f, -21.419f, 1.0f},
     {0.0f, 0.696f, -0.718f, 0.0f},
     40,
     1.0f,
     -1,
     0.0f,
     queenClothMeshUv[4],
     {0},
     {0.0f, -10.894f, -10.437f, 0.0f}},
    {15.248f,
     {0},
     {6.168f, 89.387f, -21.167f, 1.0f},
     {0.367f, 0.7f, -0.612f, 0.0f},
     40,
     1.0f,
     -1,
     0.0f,
     queenClothMeshUv[8],
     {0},
     {1.94f, -10.842f, -10.545f, 0.0f}},
    {14.351f,
     {0},
     {13.033f, 87.372f, -18.54f, 1.0f},
     {0.602f, 0.66f, -0.449f, 0.0f},
     40,
     0.7f,
     41,
     0.3f,
     queenClothMeshUv[7],
     {0},
     {4.245f, -10.646f, -8.637f, 0.0f}},
    {12.951f,
     {0},
     {17.716f, 84.158f, -11.773f, 1.0f},
     {0.87f, 0.489f, -0.068f, 0.0f},
     40,
     0.5f,
     41,
     0.5f,
     queenClothMeshUv[10],
     {0},
     {5.83f, -10.332f, -5.194f, 0.0f}},
    {11.889f,
     {0},
     {17.743f, 83.39f, -0.429f, 1.0f},
     {0.788f, 0.473f, 0.395f, 0.0f},
     40,
     0.1f,
     41,
     0.9f,
     queenClothMeshUv[11],
     {0},
     {5.722f, -10.257f, 1.841f, 0.0f}},
    {11.883f,
     {0},
     {12.811f, 84.763f, 2.835f, 1.0f},
     {0.41f, 0.429f, 0.805f, 0.0f},
     40,
     0.1f,
     41,
     0.9f,
     queenClothMeshUv[12],
     {0},
     {3.776f, -10.393f, 4.352f, 0.0f}},
    {11.74f,
     {0},
     {6.96f, 84.761f, 3.983f, 1.0f},
     {0.163f, 0.376f, 0.912f, 0.0f},
     40,
     0.1f,
     41,
     0.9f,
     queenClothMeshUv[5],
     {0},
     {2.007f, -10.392f, 5.08f, 0.0f}},
    {11.422f,
     {0},
     {0.0f, 84.683f, 4.502f, 1.0f},
     {-0.0f, 0.364f, 0.931f, 0.0f},
     40,
     1.0f,
     -1,
     0.0f,
     queenClothMeshUv[6],
     {0},
     {-0.0f, -10.384f, 4.758f, 0.0f}},
    {11.74f,
     {0},
     {-6.96f, 84.761f, 3.983f, 1.0f},
     {-0.163f, 0.376f, 0.912f, 0.0f},
     40,
     0.1f,
     45,
     0.9f,
     queenClothMeshUv[14],
     {0},
     {-2.007f, -10.392f, 5.08f, 0.0f}},
};

static Cloth4DCol queenClothMesh2Cols[13]; /* derived name */

static Cloth4DCfg queenClothMesh2 = {
    /* derived name */
    13, 16, 0, 0, 0, 0, 0, 128, queenClothTexture, queenClothMesh2Cols, 34.852f, 0,
};

static float queenClothMesh2Uv[13][16][2] = {
    /* derived name */
    {{0.402f, 0.275f},
     {0.393f, 0.323f},
     {0.384f, 0.371f},
     {0.374f, 0.418f},
     {0.365f, 0.466f},
     {0.355f, 0.514f},
     {0.346f, 0.561f},
     {0.337f, 0.609f},
     {0.327f, 0.657f},
     {0.318f, 0.704f},
     {0.309f, 0.752f},
     {0.299f, 0.8f},
     {0.29f, 0.847f},
     {0.28f, 0.895f},
     {0.271f, 0.943f},
     {0.262f, 0.99f}},
    {{0.966f, 0.277f},
     {0.968f, 0.323f},
     {0.969f, 0.37f},
     {0.97f, 0.416f},
     {0.972f, 0.463f},
     {0.973f, 0.509f},
     {0.974f, 0.556f},
     {0.975f, 0.602f},
     {0.977f, 0.649f},
     {0.978f, 0.695f},
     {0.979f, 0.742f},
     {0.98f, 0.788f},
     {0.982f, 0.835f},
     {0.983f, 0.881f},
     {0.984f, 0.927f},
     {0.986f, 0.974f}},
    {{0.016f, 0.203f},
     {0.027f, 0.254f},
     {0.038f, 0.306f},
     {0.049f, 0.357f},
     {0.06f, 0.409f},
     {0.071f, 0.46f},
     {0.082f, 0.511f},
     {0.093f, 0.563f},
     {0.104f, 0.614f},
     {0.115f, 0.665f},
     {0.126f, 0.717f},
     {0.137f, 0.768f},
     {0.148f, 0.819f},
     {0.159f, 0.871f},
     {0.17f, 0.922f},
     {0.181f, 0.973f}},
    {{0.637f, 0.324f},
     {0.633f, 0.369f},
     {0.628f, 0.413f},
     {0.624f, 0.458f},
     {0.62f, 0.502f},
     {0.616f, 0.547f},
     {0.612f, 0.591f},
     {0.608f, 0.636f},
     {0.603f, 0.68f},
     {0.599f, 0.725f},
     {0.595f, 0.77f},
     {0.591f, 0.814f},
     {0.587f, 0.859f},
     {0.583f, 0.903f},
     {0.578f, 0.948f},
     {0.574f, 0.992f}},
    {{0.334f, 0.239f},
     {0.327f, 0.289f},
     {0.32f, 0.338f},
     {0.314f, 0.388f},
     {0.307f, 0.438f},
     {0.3f, 0.488f},
     {0.293f, 0.537f},
     {0.287f, 0.587f},
     {0.28f, 0.637f},
     {0.273f, 0.687f},
     {0.266f, 0.736f},
     {0.26f, 0.786f},
     {0.253f, 0.836f},
     {0.246f, 0.886f},
     {0.239f, 0.935f},
     {0.233f, 0.985f}},
    {{0.525f, 0.238f},
     {0.52f, 0.287f},
     {0.515f, 0.335f},
     {0.509f, 0.383f},
     {0.504f, 0.432f},
     {0.499f, 0.48f},
     {0.493f, 0.528f},
     {0.488f, 0.576f},
     {0.483f, 0.625f},
     {0.477f, 0.673f},
     {0.472f, 0.721f},
     {0.467f, 0.77f},
     {0.461f, 0.818f},
     {0.456f, 0.866f},
     {0.451f, 0.915f},
     {0.445f, 0.963f}},
    {{0.68f, 0.27f},
     {0.68f, 0.318f},
     {0.681f, 0.365f},
     {0.681f, 0.412f},
     {0.682f, 0.46f},
     {0.683f, 0.507f},
     {0.683f, 0.555f},
     {0.684f, 0.602f},
     {0.684f, 0.649f},
     {0.685f, 0.697f},
     {0.686f, 0.744f},
     {0.686f, 0.791f},
     {0.687f, 0.839f},
     {0.687f, 0.886f},
     {0.688f, 0.934f},
     {0.689f, 0.981f}},
    {{0.03f, 0.914f},
     {0.029f, 0.918f},
     {0.027f, 0.922f},
     {0.025f, 0.926f},
     {0.024f, 0.93f},
     {0.022f, 0.934f},
     {0.02f, 0.938f},
     {0.019f, 0.942f},
     {0.017f, 0.946f},
     {0.015f, 0.95f},
     {0.014f, 0.954f},
     {0.012f, 0.958f},
     {0.01f, 0.962f},
     {0.009f, 0.966f},
     {0.007f, 0.97f},
     {0.005f, 0.974f}},
    {{0.076f, 0.251f},
     {0.072f, 0.3f},
     {0.069f, 0.349f},
     {0.065f, 0.398f},
     {0.062f, 0.446f},
     {0.058f, 0.495f},
     {0.055f, 0.544f},
     {0.051f, 0.593f},
     {0.048f, 0.641f},
     {0.044f, 0.69f},
     {0.041f, 0.739f},
     {0.037f, 0.788f},
     {0.034f, 0.836f},
     {0.03f, 0.885f},
     {0.027f, 0.934f},
     {0.023f, 0.982f}},
    {{0.838f, 0.275f},
     {0.836f, 0.322f},
     {0.834f, 0.369f},
     {0.832f, 0.416f},
     {0.83f, 0.462f},
     {0.828f, 0.509f},
     {0.826f, 0.556f},
     {0.825f, 0.603f},
     {0.823f, 0.65f},
     {0.821f, 0.697f},
     {0.819f, 0.744f},
     {0.817f, 0.791f},
     {0.815f, 0.838f},
     {0.813f, 0.885f},
     {0.812f, 0.932f},
     {0.81f, 0.979f}},
    {{0.356f, 0.194f},
     {0.36f, 0.245f},
     {0.365f, 0.297f},
     {0.369f, 0.348f},
     {0.373f, 0.4f},
     {0.378f, 0.451f},
     {0.382f, 0.503f},
     {0.386f, 0.554f},
     {0.391f, 0.606f},
     {0.395f, 0.657f},
     {0.399f, 0.709f},
     {0.404f, 0.76f},
     {0.408f, 0.812f},
     {0.412f, 0.863f},
     {0.417f, 0.915f},
     {0.421f, 0.966f}},
    {{0.53f, 0.219f},
     {0.531f, 0.269f},
     {0.533f, 0.32f},
     {0.535f, 0.37f},
     {0.537f, 0.421f},
     {0.538f, 0.471f},
     {0.54f, 0.522f},
     {0.542f, 0.572f},
     {0.544f, 0.623f},
     {0.545f, 0.673f},
     {0.547f, 0.724f},
     {0.549f, 0.774f},
     {0.551f, 0.825f},
     {0.552f, 0.876f},
     {0.554f, 0.926f},
     {0.556f, 0.977f}},
    {{0.008f, 0.863f},
     {0.011f, 0.871f},
     {0.014f, 0.878f},
     {0.016f, 0.886f},
     {0.019f, 0.893f},
     {0.022f, 0.901f},
     {0.025f, 0.909f},
     {0.028f, 0.916f},
     {0.031f, 0.924f},
     {0.034f, 0.931f},
     {0.036f, 0.939f},
     {0.039f, 0.946f},
     {0.042f, 0.954f},
     {0.045f, 0.961f},
     {0.048f, 0.969f},
     {0.051f, 0.977f}},
};

static Cloth4DCol queenClothMesh2Cols[13] = {
    /* derived name */
    {2.609f,
     {0},
     {-53.885f, 143.321f, -5.563f, 1.0f},
     {0.102f, 0.52f, 0.848f, 0.0f},
     20,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[7],
     {0},
     {-2.371f, 0.923f, -0.578f, 0.0f}},
    {6.062f,
     {0},
     {-46.821f, 144.768f, -7.302f, 1.0f},
     {0.128f, 0.58f, 0.805f, 0.0f},
     20,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[8],
     {0},
     {-4.979f, 1.591f, -3.07f, 0.0f}},
    {9.557f,
     {0},
     {-32.474f, 145.372f, -11.092f, 1.0f},
     {0.012f, 0.869f, 0.495f, 0.0f},
     19,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[4],
     {0},
     {-6.21f, 0.855f, -7.214f, 0.0f}},
    {12.18f,
     {0},
     {-28.467f, 145.58f, -15.865f, 1.0f},
     {0.179f, 0.94f, -0.291f, 0.0f},
     19,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[5],
     {0},
     {-6.111f, -2.391f, -10.261f, 0.0f}},
    {14.214f,
     {0},
     {-17.627f, 132.789f, -20.177f, 1.0f},
     {0.136f, 0.546f, -0.827f, 0.0f},
     18,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[0],
     {0},
     {-5.552f, -6.954f, -11.084f, 0.0f}},
    {15.347f,
     {0},
     {-8.462f, 129.807f, -22.04f, 1.0f},
     {-0.196f, 0.421f, -0.886f, 0.0f},
     18,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[3],
     {0},
     {-2.878f, -9.596f, -11.626f, 0.0f}},
    {16.54f,
     {0},
     {-0.001f, 128.527f, -23.174f, 1.0f},
     {0.0f, 0.277f, -0.961f, 0.0f},
     1,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[1],
     {0},
     {0.077f, -11.838f, -11.551f, 0.0f}},
    {15.333f,
     {0},
     {8.453f, 129.807f, -22.04f, 1.0f},
     {0.203f, 0.416f, -0.886f, 0.0f},
     2,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[9],
     {0},
     {2.802f, -9.596f, -11.626f, 0.0f}},
    {13.888f,
     {0},
     {17.616f, 132.787f, -20.177f, 1.0f},
     {-0.125f, 0.529f, -0.839f, 0.0f},
     2,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[6],
     {0},
     {4.362f, -6.904f, -11.233f, 0.0f}},
    {11.604f,
     {0},
     {28.706f, 145.453f, -16.137f, 1.0f},
     {-0.165f, 0.946f, -0.28f, 0.0f},
     3,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[11],
     {0},
     {4.904f, -2.383f, -10.243f, 0.0f}},
    {9.074f,
     {0},
     {32.632f, 145.253f, -11.155f, 1.0f},
     {-0.024f, 0.825f, 0.565f, 0.0f},
     3,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[10],
     {0},
     {5.466f, 0.866f, -7.191f, 0.0f}},
    {5.437f,
     {0},
     {46.704f, 144.817f, -8.304f, 1.0f},
     {-0.111f, 0.611f, 0.784f, 0.0f},
     4,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[2],
     {0},
     {4.207f, 1.588f, -3.056f, 0.0f}},
    {2.531f,
     {0},
     {53.846f, 143.068f, -6.081f, 1.0f},
     {-0.108f, 0.583f, 0.806f, 0.0f},
     4,
     1.0f,
     -1,
     0.0f,
     queenClothMesh2Uv[12],
     {0},
     {2.297f, 0.913f, -0.543f, 0.0f}},
};

/* the five points the queen's cape hangs from */
static ClothHangCfg queenClothHang[6] = {
    /* derived name */
    {1, -5.0f, 45.0f, 10.0f, 49, {0}, 0.0f, 0.0f, {0}, 1.0f, 1.0f, {0}},
    {1, -5.0f, 60.0f, 10.0f, 50, {0}, 0.0f, 0.0f, {0}, 1.0f, 1.0f, {0}},
    {1, -5.0f, 45.0f, 10.0f, 45, {0}, 0.0f, 0.0f, {0}, 1.0f, 1.0f, {0}},
    {1, -5.0f, 60.0f, 10.0f, 46, {0}, 0.0f, 0.0f, {0}, 1.0f, 1.0f, {0}},
    {1, -20.0f, 45.0f, 10.0f, 44, {0}, 0.0f, 0.0f, {0}, 1.0f, 1.0f, {0}},
    {-1, 0.0f, 0.0f, 0.0f, 0, {0}, 0.0f, 0.0f, {0}, 0.0f, 0.0f, {0}},
};

/* the six places an enemy is dropped back into the last stage when it is
   called again; LW coordinates, converted by lw_pos_to_ico_pos at the site */
static float queenSpawnPos[6][4] = {
    /* derived name */
    {1200.0f, -1000.0f, 300.0f, 0.0f}, {1200.0f, -1000.0f, -100.0f, 0.0f},
    {1100.0f, -1000.0f, 100.0f, 0.0f}, {800.0f, -920.0f, 200.0f, 0.0f},
    {600.0f, -900.0f, -200.0f, 0.0f},  {400.0f, -900.0f, 100.0f, 0.0f},
};

/* the queen's own frame counter, the timestamp every wait in her state
   machine is measured against */
static int queenFrame; /* derived name */

/* the 3x3 identity, for QueenBarrierGeo and QueenBallGeo */
static inline void UnitMatrix33(QMat3 *m) /* derived name */
{
    m->x.f[0] = 1.0f;
    m->x.f[1] = 0.0f;
    m->x.f[2] = 0.0f;
    m->x.f[3] = 0.0f;
    m->y.f[0] = 0.0f;
    m->y.f[1] = 1.0f;
    m->y.f[2] = 0.0f;
    m->y.f[3] = 0.0f;
    m->z.f[0] = 0.0f;
    m->z.f[1] = 0.0f;
    m->z.f[2] = 1.0f;
    m->z.f[3] = 0.0f;
}

static void scale_m34(LVec *dst, void *src, float f)
{
    sceVu0CopyMatrix(dst, src);
    sceVu0ScaleVector(dst, dst, f);
    sceVu0ScaleVector(dst + 1, dst + 1, f);
    return sceVu0ScaleVector(dst + 2, dst + 2, f);
}

/* this file's own effect_end_func; itou_boss.c has its own */

static void effect_end_func(int no)
{
    GObj *g = isysGObjSearchFromObjKindID_begin(47);
    GObj *weapon = GOBJ_ACT(boyGObj)->weapon;

    if (g != 0) {
        ((QueenWork *)GOBJ_SUB(g)->work)->power += 1;
    }
    if (weapon != 0) {
        LightTorchOnOfWeapon(weapon);
    }
}

/* The ten rate tables come in two parallel sets of five, the first used when stage_no is
   37 (the queen's own stage, st25a) and the second everywhere else; each is
   indexed by the ball's phase counter at ballw+0x18, which runs 0 to 10. */
typedef struct QueenUVScroll { /* field names derived */
    float v[6];
} QueenUVScroll; /* derived name */

static const float genWaitRateSt25[11] /* derived name */ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                                             1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

static const float ballWaitRateSt25[11] /* derived name */ = {7.0f, 6.0f, 6.0f, 5.0f, 5.0f, 4.0f,
                                                              4.0f, 4.0f, 4.0f, 4.0f, 4.0f};

static const float ballHoldRateSt25[11] /* derived name */ = {5.0f, 5.0f, 5.0f, 5.0f, 5.0f, 5.0f,
                                                              5.0f, 5.0f, 5.0f, 5.0f, 5.0f};

static const float ballSpeedRateSt25[11] /* derived name */ = {0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f,
                                                               0.6f, 0.6f, 0.6f, 0.6f, 0.6f};

static const QueenUVScroll ballUVScrollSt25[11] = {
    /* derived name */
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}}, {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}}, {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}}, {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}}, {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}}, {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
};

static const float genWaitRateDefault[11] /* derived name */ = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                                                1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

static const float ballWaitRateDefault[11] /* derived name */ = {3.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
                                                                 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

static const float ballHoldRateDefault[11] /* derived name */ = {4.0f, 4.0f, 4.0f, 4.0f, 4.0f, 4.0f,
                                                                 4.0f, 4.0f, 4.0f, 4.0f, 4.0f};

static const float ballSpeedRateDefault[11] /* derived name */ = {
    0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f, 0.6f};

static const QueenUVScroll ballUVScrollDefault[11] = {
    /* derived name */
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}}, {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}}, {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}}, {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}}, {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}}, {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
    {{0.0f, 0.0f, 0.001f, 0.01f, 0.99f, 0.99f}},
};

static const char queenAttackedMsg[] = "queen attacked\n"; /* derived name */

static const char enemyDeadMsg[] = "enemy dead %p\n"; /* derived name */

void queenBeforeFunc(GObj *g)
{
    QVec pos;
    QVec target;
    GObjMailQueue *q = (GObjMailQueue *)&g->mailBox;
    QueenWork *w = GOBJ_SUB(g)->work;
    Act *act = GOBJ_ACT(g);
    int i;

    for (i = 0; i < q->num; i++) {
        GObjMailEntry *e = &q->e[i];

        switch (e->mail) {
        case 46:
            w->st.f.pause = 1;
            break;
        case 47:
            w->st.f.pause = 0;
            break;
        case 13:
            if (scpGameStat_BoyWeaponkind() == 5) {
                GObj *o;

                debug_StdPrintfDummy(queenAttackedMsg);
                w->st.f.damaged = 1;
                o = isysGObjSearchFromObjKindID_begin(53);
                if (o != 0) {
                    ((QueenBallWork *)GOBJ_SUB(o)->work)->cancel = 1;
                }
            }
            break;
        case 18: {
            GObj *boy = GOBJ_ACT(boyGObj)->weapon;

            debug_StdPrintfDummy(enemyDeadMsg, boy);
            if (e->data != 0 && boy != 0) {
                GetRootPosition(pos.f, e->data);
                GetRootPosition(target.f, boy);
                GatherEffect_Set(12, &pos, IdentityQuaternion, &target, 2.5f, effect_end_func);
            }
            break;
        }
        }
    }
    q->num = 0;
    /* the actor's motion orient request, refreshed from the copy the
       sub-object keeps at 0x180 */
    act->env.motOriReq = *(MotOriReq *)&GOBJ_SUB(g)->root.wall;
}

typedef struct QueenGenTable { /* field names derived */
    /* 0x0 */ int n;
    /* 0x4 */ const int *list;
} QueenGenTable; /* derived name */

/* The layout ids gene_enemy picks a spawn point from, one list per stage set. */
static const int genEnemyLayoutSt25[6] = {2152, 2153, 2154, 2155, 2156, 2157}; /* derived name */

static const int genEnemyLayoutDefault[6] = {3535, 3536, 3537, 3538, 3539, 3540}; /* derived name */

static const QueenGenTable genEnemyTable[2] = {
    /* derived name */
    {6, genEnemyLayoutSt25},
    {6, genEnemyLayoutDefault},
};

static const char genEnemyStatFmt[] = "n_enemy_max:%d n_enemy:%d counter:%d"; /* derived name */

void gene_enemy(volatile ICO_WORD g)
{
    union { /* field names derived */
        float f[4];
        int i[4];
    } pos;

    QueenWork *w = GOBJ_SUB(g)->work;
    const QueenGenTable *tbl;
    GObj *o;
    GObj *c;
    GObj *e;
    int num;
    int total;
    int alive;
    int timer;
    int wait;
    int k;
    GObj *obj;

    o = isysGObjSearchFromObjKindID_begin(54);
    num = (o != 0) ? ((QueenBarrierWork *)GOBJ_SUB(o)->work)->damage : 0;
    tbl = (stage_no == 37) ? &genEnemyTable[0] : &genEnemyTable[1];

    timer = 0;
    total = 0;
    for (c = isysGObjSearchFromObjKindID_begin(4); c != 0;
         c = isysGObjSearchFromObjKindID_next(c)) {
        total++;
    }
    _ACTWait(1);

    for (;;) {
        if ((w->st.all & 0xFF0000FF) == 0 && w->st.f.attack != 0) {
            alive = 0;
            e = isysGObjSearchFromObjKindID_begin(4);
            while (e != 0 && isEnemyHyde(e) == 0) {
                alive++;
                e = isysGObjSearchFromObjKindID_next(e);
            }
            if (debug_font_flag & 1) {
                debug_Printf(10, 90, -1, genEnemyStatFmt, total, alive, timer);
            }
            if (alive < total) {
                if (timer > ((stage_no == 37) ? genWaitRateSt25 : genWaitRateDefault)[num] *
                                ((60 - systemStatus[0] * 10) / systemStatus[1])) {
                    if (stage_no == 37) {
                        obj =
                            isysGObjSearchFromObjLayoutID(tbl->list[(int)(_GetRandom() * tbl->n)]);
                        if (obj != 0) {
                            lw_pos_to_ico_pos(pos.f, queenSpawnPos[(int)(_GetRandom() * 6.0f)]);
                            SetRootPosition(obj, pos.f);
                            wait = (int)(((60 - systemStatus[0] * 10) / systemStatus[1]) * 0.5f);
                            for (k = 0; k <= wait; k++) {
                                _ACTWait(1);
                            }
                            Generator_Call(obj);
                            _ACTWait(1);
                            while (stage_CheckAnimationFinish(506) == 0) {
                                _ACTWait(1);
                            }
                            for (k = 0; k <= wait; k++) {
                                _ACTWait(1);
                            }
                            pos.f[0] = 4294967296.0f;
                            pos.f[1] = 4294967296.0f;
                            pos.f[2] = 4294967296.0f;
                            pos.i[3] = 0;
                            SetRootPosition(obj, pos.f);
                        }
                        timer = 0;
                    }
                }
            }
            timer++;
        }
        _ACTWait(1);
    }
}

/* The position the queen is dropped at outside her own stage. */
static const QVec queenStartPos = {{0.0f, 800.0f, 0.0f, 1.0f}}; /* derived name */

static const char queenDeadMsg[] = "queen dead\n"; /* derived name */

static const char queenBallScrTexture[] = "queen_ball_scr"; /* derived name */

/* One status slot: the motion-parameter words the actor extension keeps are
 * read as a float here and as an int elsewhere. */
typedef union QueenVal { /* field names derived */
    int i;
    float f;
    char *motReq; /* the actor's 0x130: the motion record SetMotionRequest returns */
} QueenVal;       /* derived name */

/* The look-at block the queen's motion system keeps in her actor parameter
 * area (gobj->x15C): a world-space target the head and body steer toward, and
 * the slot that enables it.  Like every other slot of that parameter block the
 * enable is a QueenVal (the block's words are written as int here and read as
 * float by the motion evaluator), and the target is a QVec. */
typedef struct QueenLookAt { /* field names derived */
    /* 0x00 */ QueenVal on;
    /* 0x04 */ QueenVal pad04[3];
    /* 0x10 */ QVec pos;
} QueenLookAt; /* derived name */

/* The queen's per-frame motion-status record, refreshed from the actor
 * extension at gobj->x15C every tick. */
typedef struct QueenStatus { /* field names derived */
    /* 0x00 */ int motion;
    /* 0x04 */ int prevMotion;
    /* 0x08 */ QueenVal ratio;
    /* 0x0C */ QueenVal prevRatio;
    /* 0x10 */ int step;
    /* 0x14 */ int prevStep;
    /* 0x18 */ int changed;
    /* 0x1C */ int active;
    /* 0x20 */ int count;
    /* 0x24 */ char pad24[12];
} QueenStatus; /* derived name */

static inline void QueenStatusUpdate(GObj *g, QueenStatus *st) /* derived name */
{
    st->prevMotion = st->motion;
    st->prevRatio.f = st->ratio.f;
    st->prevStep = st->step;

    st->motion = GOBJ_SUB(g)->ctrl.motion;
    st->ratio.f = GOBJ_SUB(g)->ctrl.animFrame;
    st->step = GOBJ_SUB(g)->ctrl.frameEnd;
    st->changed = 0;
    if (st->motion != st->prevMotion) {
        st->count = 1;
        st->changed = 1;
    }
    if (st->motion == st->prevMotion) {
        if (st->prevStep != 0) {
            st->count++;
        }
    }
    st->active = (st->changed != 0 || st->step != 0) ? 1 : 0;
}

static inline void QueenStatusRestart(GObj *g, QueenStatus *st) /* derived name */
{
    QueenStatusUpdate(g, st);
    st->changed = 0;
    st->active = 1;
    st->count = 1;
}

/* QueenStartAttack's body, for the caller above its definition */
static inline void QueenStartAttack_inl(int flag) /* derived name */
{
    GObj *g;

    g = isysGObjSearchFromObjKindID_begin(47);
    ((QueenWork *)GOBJ_SUB(g)->work)->st.f.attack = flag;

    g = isysGObjSearchFromObjKindID_begin(54);
    while (g != 0) {
        ((QueenBarrierWork *)GOBJ_SUB(g)->work)->active = 1;
        g = isysGObjSearchFromObjKindID_next(g);
    }
}

/* The queen's brain thread.  `g` is volatile because this body is an actor
 * coroutine: _ACTWait() unwinds and resumes it, and the actor system can move
 * the GObj between resumes, so the thread's own copy in its frame is re-read at
 * every use rather than cached in a register.
 */
void subQueenBrainMain(volatile ICO_WORD g)
{
    QueenStatus st;
    QVec pos;
    QVec rootPos;
    QVec dir;
    LVec target;
    QueenWork *w;
    int motionOk;
    int first;
    int wait;
    char *ext;
    int startFrame;
    GObj *ball;
    GObj *barrier;
    QueenBallWork *ballw;
    QueenBarrierWork *barrierw;
    QueenWork *qw;
    GObj *boy;
    const QueenUVScroll *uv;

    /* the actor record: the motion record SetMotionRequest returns is kept
       in the QueenVal slot at 0x130, the request it takes is Act's
       motOriReq */
    ext = (char *)GOBJ_ACT(g);
    w = GOBJ_SUB(g)->work;

    motionOk = 0;
    first = 1;
    startFrame = queenFrame;
    wait = 1000;

    _ACTWait(1);
    if (stage_no != 37) {
        pos = queenStartPos;
        SetDirectRootPosition(boyGObj, &pos);
        QueenStartAttack_inl(first);
    }
    QueenStatusRestart((GObj *)g, &st);

    for (;;) {
        ball = isysGObjSearchFromObjKindID_begin(53);
        barrier = isysGObjSearchFromObjKindID_begin(54);
        QueenStatusUpdate((GObj *)g, &st);
        if (debug_font_flag & 1) {
            debug_Printf(10, 80, -1, "barr %d", InqQueenBarrierExist());
        }
        if ((w->st.all & 0xFF0000FF) == 0 && w->st.f.attack != 0 && ball != 0 && barrier != 0) {
            qw = GOBJ_SUB(g)->work;

            GetRootPosition(rootPos.f, (GObj *)g);
            _GetMotionDirection(dir.f, (GObj *)g);
            ballw = GOBJ_SUB(ball)->work;
            barrierw = GOBJ_SUB(barrier)->work;

            if (qw->st.f.damaged != 0 && barrierw->active == 0) {
                qw->st.f.dead = 1;
                debug_StdPrintfDummy(queenDeadMsg);
            }

            if (barrierw->react != 0) {
                if ((ICO_RAW(QueenVal, ext, 0x130, *(QueenVal *)&((Act *)ext)->motReq).motReq =
                         SetMotionRequest((char *)g, 326, ((Act *)ext)->env.motOriReq)) != 0) {
                    barrierw->react = 0;
                }
            }

            switch (GOBJ_SUB(g)->ctrl.motion) {
            case 1072:
            case 1077:
            case 1078:
                GetRootPosition(target.v, boyGObj);
                GOBJ_SUB(g)->root.lookPos[0] = target.v[0];
                GOBJ_SUB(g)->root.lookPos[1] = target.v[1];
                GOBJ_SUB(g)->root.lookPos[2] = target.v[2];
                GOBJ_SUB(g)->root.lookMode = 1;
            }

            switch (GOBJ_SUB(g)->ctrl.motion) {
            case 1073:
            case 1074:
            case 1075:
            case 1076:
            default:
                ICO_RAW(QueenVal, ext, 0x130, *(QueenVal *)&((Act *)ext)->motReq).motReq =
                    SetMotionRequest((char *)g, 1, ((Act *)ext)->env.motOriReq);
                break;

            case 1072:
                motionOk = 1;
                if ((ICO_RAW(QueenVal, ext, 0x130, *(QueenVal *)&((Act *)ext)->motReq).motReq =
                         SetMotionRequest((char *)g, 324, ((Act *)ext)->env.motOriReq)) != 0) {
                    if (first) {
                        startFrame = queenFrame;
                        wait = (int)(*((stage_no == 37) ? &ballWaitRateSt25[barrierw->damage]
                                                        : &ballWaitRateDefault[barrierw->damage]) *
                                     ((60 - systemStatus[0] * 10) / systemStatus[1]));
                    }
                    first = 0;
                }
                break;

            case 1077:
                if (ballw->busy == 0 && motionOk != 0 && queenFrame - startFrame >= wait) {
                    ICO_RAW(QueenVal, ext, 0x130, *(QueenVal *)&((Act *)ext)->motReq).motReq =
                        SetMotionRequest((char *)g, 325, ((Act *)ext)->env.motOriReq);
                }
                break;

            case 1078:
                ICO_RAW(QueenVal, ext, 0x130, *(QueenVal *)&((Act *)ext)->motReq).motReq =
                    SetMotionRequest((char *)g, 1, ((Act *)ext)->env.motOriReq);
                if (GOBJ_SUB(g)->ctrl.animFrame > 15.0f && ballw->busy == 0 && motionOk != 0) {
                    uv = (stage_no == 37) ? &ballUVScrollSt25[barrierw->damage]
                                          : &ballUVScrollDefault[barrierw->damage];

                    motionOk = 0;
                    sceVu0ScaleVectorXYZ(&target, &dir, 100.0f);
                    sceVu0AddVector(&target, &rootPos, &target);
                    SetDirectRootPosition(ball, &target);
                    ballw->busy = 1;
                    ballw->live = 1;
                    ballw->scale = 0.0f;
                    ballw->hit = 0;
                    startFrame = queenFrame;
                    wait = (int)(*((stage_no == 37) ? &ballWaitRateSt25[barrierw->damage]
                                                    : &ballWaitRateDefault[barrierw->damage]) *
                                 ((60 - systemStatus[0] * 10) / systemStatus[1]));
                    tex_SetUVScroll(queenBallScrTexture, uv->v[0], uv->v[1], uv->v[2], uv->v[3],
                                    uv->v[4], uv->v[5], 1);
                }
                break;

            case 1079:
                startFrame = queenFrame;
                wait = (int)(*((stage_no == 37) ? &ballHoldRateSt25[barrierw->damage]
                                                : &ballHoldRateDefault[barrierw->damage]) *
                             ((60 - systemStatus[0] * 10) / systemStatus[1]));
                ICO_RAW(QueenVal, ext, 0x130, *(QueenVal *)&((Act *)ext)->motReq).motReq =
                    SetMotionRequest((char *)g, 1, ((Act *)ext)->env.motOriReq);
                break;
            }
        }
        boy = GOBJ_ACT(boyGObj)->weapon;
        if (boy != 0) {
            GetRootPosition(target.v, boy);
            ParticleEffects_SetAllGoal(&target);
        }
        ((QueenWork *)GOBJ_SUB(g)->work)->st.f.damaged = 0;
        _ACTWait(1);
    }
}

static const char queenFile[] = __FILE__; /* derived name */

static const char queenBarrierAttackedMsg[] = "queen barrier attacked\n"; /* derived name */

/* the barrier's own spawn layout id, one per stage set */
static const int barrierLayoutSt25[] = {2150}; /* derived name */

static const int barrierLayoutDefault[] = {3527}; /* derived name */

static const char damageFmt[] = "damage:%d"; /* derived name */

static const char mailFmt[] = "mail %d\n"; /* derived name */

static const char queenBallAttackedMsg[] = "queen ball attacked\n"; /* derived name */

/* this file's own Debug_StickControl; act_bird.c has its own */
static void Debug_StickControl(GObj *self)
{
    QVec dir;
    Act *ext = GOBJ_ACT(self);

    if (self == CurrentTargetGObj) {
        iosPadConnect(&ext->pad, 0, 0, &ext->padConf);
        iosPadRead(&ext->pad);
        iosPadGetStick(&ext->pad, &ext->stick, 0, 2, 2, 0);
        _GetMotionDirection(dir.f, self);
        ext->stick.angle = CorrectStickInfo(&dir, &ext->stick);
        if (ext->stick.mag > 0.001f) {
            ConvertStickToAbsCoord(ext->dir, &ext->stick);
        }
    } else if (self == CurrentTargetGObjSub) {
        iosPadConnect(&ext->pad, 0, 1, &ext->padConf);
    } else {
        iosPadConnect(&ext->pad, 0, 1, &ext->padConf);
    }
}

void *InitQueenGeo(GObj *g)
{
    Sub15C *ext = GOBJ_SUB(g);
    QueenWork *w;
    int i;

    w = iosMallocDebug(ios_partition_sugipon, sizeof(QueenWork), queenFile, 732);
    memset(w, 0, sizeof(QueenWork));
    for (i = 3; i >= 0; i--) {
        queenBga[i] = 0;
    }
    w->clothOn = 1;
    w->cloth = InitCloth4D(g, &queenClothMesh, queenClothHang);
    w->cloth2 = InitCloth4D(g, &queenClothMesh2, 0);
    ext->work = w;
    InitMotionOrient(g, 2407, 2421, 12, 24, 1072);
    SetLodLevel(g, 2);
    actInitialize(g);
    actInitialize_ext_charcter(g);
    return w;
}

void QueenGeo(GObj *g)
{
    QueenWork *w;

    if (systemStatus[5] == 0) {
        queenFrame++;
    }
    ExecMotionOrient(g);
    SetActressLight(g, 35, 44, 472);
    w = GOBJ_SUB(g)->work;
    if (w->clothOn != 0) {
        GetCloth4D(w->cloth, 3.0f, 0.98f);
        GetCloth4D(w->cloth2, 5.0f, 0.9f);
    }
    CylinderCollision(g, 1, 100.0f, 100.0f, 0.001f);
}

void QueenDL(GObj *g)
{
    QueenWork *w;

    if (boyGObj != 0) {
        ACTDispLwsBoyStonize_InQueenStage(boyGObj);
    }
    p2o_SetDefaultEnviroment();
    p2o_DispVU1(g);
    w = GOBJ_SUB(g)->work;
    if (w->clothOn != 0) {
        DispCloth4D(w->cloth, (char *)GOBJ_SUB(g)->lightMtx + 0x40, (char *)GOBJ_SUB(g)->lightMtx);
    }
    DispCloth4D(w->cloth2, (char *)GOBJ_SUB(g)->lightMtx + 0x40, (char *)GOBJ_SUB(g)->lightMtx);
}

/* an angle wrapped into one turn */
static inline float WrapRad(float a) /* derived name */
{
    a = fmodf(a, 6.2831854820251465f);
    if (a > 3.1415927410125732f) {
        a -= 6.2831854820251465f;
    } else if (a < -3.1415927410125732f) {
        a += 6.2831854820251465f;
    }
    return a;
}

void QueenBarrierGeo(GObj *g)
{
    QVec pos;
    QVec rootPos;
    QMat44 m1;
    QMat3T rt;
    QVec ofs;
    QVec axis;
    QMat44 m3;
    QueenBarrierWork *w;
    GObj *queen;
    QueenWork *qw;
    const int *tbl;
    unsigned int i;
    unsigned int found;
    unsigned int mine;

    memset(&pos, 0, sizeof(pos));
    pos.f[1] = 2000.0f;
    w = GOBJ_SUB(g)->work;
    queen = isysGObjSearchFromObjKindID_begin(47);
    qw = GOBJ_SUB(queen)->work;
    if (stage_no == 37) {
        tbl = barrierLayoutSt25;
    } else {
        tbl = barrierLayoutDefault;
    }
    mine = 0;
    if (debug_font_flag & 1) {
        debug_Printf(10, 70, 0xFFFFFFFF, damageFmt, w->damage);
    }
    for (i = 0; i < 1; i++) {
        GObj *o = isysGObjSearchFromObjLayoutID(tbl[i]);

        if (o == 0) {
            continue;
        }
        if (((QueenBarrierWork *)GOBJ_SUB(o)->work)->active != 0) {
            continue;
        }
        break;
    }
    found = i;
    for (i = 0; i < 1; i++) {
        if (g->labelId == tbl[i]) {
            mine = i;
            break;
        }
    }
    GetRootPosition(rootPos.f, queen);
    if (w->active == 0 || queen->active == 0 || (qw->st.all & 0xFF0000FF) != 0 ||
        qw->st.f.attack == 0) {
        GetRootMatrix(&m1, g);
        sceVu0CopyVector(&m1.w, &pos);
        CopyMatrix((void *)GOBJ_SUB(g)->nodeMtx, &m1);
    } else {
        axis.f[0] = 0.05235987901687622f;
        axis.f[1] = 0.0872664675116539f;
        axis.f[2] = 0.12217305600643158f;
        axis.f[3] = 0.0f;
        ofs = axis;
        sceVu0ScaleVector(&ofs, &ofs,
                          (float)(unsigned int)(-mine) * 0.019999999552965164f +
                              0.05000000074505806f);
        sceVu0AddVector(w->rot, w->rot, &ofs);
        w->rot[0] = WrapRad(w->rot[0]);
        w->rot[1] = WrapRad(w->rot[1]);
        w->rot[2] = WrapRad(w->rot[2]);
        UnitMatrix33(&rt.rot);
        sceVu0CopyVector(&rt.trans, w->pos);
        CopyMatrix((void *)GOBJ_SUB(g)->nodeMtx, &rt);
    }
    if (w->hit != 0 && mine == found - 1) {
        void *weapon;

        qw->power = 0;
        weapon = (void *)GOBJ_ACT(boyGObj)->weapon;
        if (weapon != 0) {
            LightTorchOffOfWeapon(weapon);
        }
        qw->wait = 18;
        ExecuteSEPackage(g, 95);
        w->damage = w->damage + 1;
        if (w->damage >= 5) {
            GetRootMatrix(&m3, g);
            sceVu0CopyVector(&m3.w, &pos);
            CopyMatrix((void *)GOBJ_SUB(g)->nodeMtx, &m3);
            w->active = 0;
            ExecuteSEPackage(boyGObj, 98);
        }
    }
    if (qw->wait > 0) {
        qw->wait = qw->wait - 1;
    }
    w->hit = 0;
    queen_barrier_anim();
}

void QueenBarrierDL(GObj *g)
{
    QueenBarrierWork *b = GOBJ_SUB(g)->work;
    if (b->active) {
        queen_barrier_disp_proc(g, 1.0f - b->damage / 5.0f);
    }
}

/* put the effect at `to`, its z axis level and pointing back at `from`;
 * for QueenBallGeo and QueenBallDL */
static inline void SetQueenBallOrient(BgaPlayNode *o, QVec *from, QVec *to) /* derived name */
{
    QVec side;
    QVec up = {{0.0f, 1.0f, 0.0f, 1.0f}};
    QVec dir;
    QMat44 m;

    sceVu0CopyVector(o->pos, to);
    sceVu0SubVector(&dir, from, to);
    dir.f[1] = 0.0f;
    sceVu0Normalize(&dir, &dir);
    sceVu0OuterProduct(&side, &up, &dir);
    sceVu0CopyVector(&m.x, &side);
    sceVu0CopyVector(&m.y, &up);
    sceVu0CopyVector(&m.z, &dir);
    ico_m33_to_quat(o->rot, &m);
}

static inline void StartQueenBallEffect(BgaPlayNode **bga, int id, QVec *from,
                                        QVec *to) /* derived name */
{
    if (*bga == 0) {
        pbga_start(bga, id);
        SetQueenBallOrient(*bga, from, to);
    }
}

static inline void CheckQueenBallRing(BgaPlayNode **bga, int id, QVec *from, QVec *to,
                                      float r) /* derived name */
{
    float d = _GetLength(to, from);
    int in = (d < r && r < d + 100.0f);

    if (in) {
        StartQueenBallEffect(bga, id, from, to);
    }
}

/* The parameterised box test.  Builds the frame that looks from `from`
 * towards `pos`, transforms `target` into it and reports whether the target
 * sits inside the box around `pos`.  The half-width and the near plane are
 * ints, the half-height and the far plane floats. */
static inline int CheckQueenBallBox(QVec *pos, QVec *from, QVec *target, int xl, float yl, int zmin,
                                    float zmax) /* derived name */
{
    QVec side;
    QVec up = {{0.0f, 1.0f, 0.0f, 1.0f}};
    QVec dir;
    QMat44 m;
    QMat44 inv;
    QVec out;
    int hit = 0;

    sceVu0SubVector(&dir, pos, from);
    dir.f[1] = 0.0f;
    sceVu0Normalize(&dir, &dir);
    sceVu0OuterProduct(&side, &up, &dir);
    sceVu0CopyVector(&m.x, &side);
    sceVu0CopyVector(&m.y, &up);
    sceVu0CopyVector(&m.z, &dir);
    sceVu0CopyVector(&m.w, pos);
    m.x.f[3] = m.y.f[3] = m.z.f[3] = 0.0f;
    m.w.f[3] = 1.0f;
    sceVu0InversMatrix(&inv, &m);
    apply_matrix_w1(&out, &inv, target);
    if (__builtin_fabsf(out.f[0]) < xl && __builtin_fabsf(out.f[1]) < yl && out.f[2] >= zmin &&
        out.f[2] < zmax) {
        hit = 1;
    }
    return hit;
}

void QueenBallGeo(GObj *g)
{
    /* the root matrix */
    float m[4][4];
    QVec queenPos;
    int num;
    int i;
    GObj *weapon;
    QueenBallWork *w;
    GObj *barrier;
    GObj *o;
    GObj *sword;
    Act *act;
    BgaPlayNode **bga;
    int hit;
    float r;

    w = GOBJ_SUB(g)->work;
    barrier = isysGObjSearchFromObjKindID_begin(54);
    num = (barrier != 0) ? ((QueenBarrierWork *)GOBJ_SUB(barrier)->work)->damage : 0;
    r = w->scale * 100.0f;
    weapon = GOBJ_ACT(boyGObj)->weapon;
    GetRootMatrix(m, g);
    GetRootPosition(queenPos.f, boyGObj);
    i = 0;
    act = GOBJ_ACT(boyGObj);
    hit = (act->actMode == 49);
    if (w->live != 0) {
        for (o = isysGObjSearchFromObjKindID_begin(17); o != 0;
             o = isysGObjSearchFromObjKindID_next(o), i++) {
            QVec objPos;

            GetRootPosition(objPos.f, o);
            hit |= CheckQueenBallBox(&objPos, (QVec *)m[3], &queenPos, 130, 300.0f, -120, 600.0f);
            bga = &queenBga[i];
            CheckQueenBallRing(bga, 482, (QVec *)m[3], &objPos, r);
            if (*bga != 0) {
                SetQueenBallOrient(*bga, (QVec *)m[3], &objPos);
            }
        }
        if (w->live != 0) {
            sword = isysGObjSearchFromObjKindID_begin(14);
            if (weapon == 0 && sword != 0) {
                QVec objPos;

                GetRootPosition(objPos.f, sword);
                hit |=
                    CheckQueenBallBox(&objPos, (QVec *)m[3], &queenPos, 75, 300.0f, -150, 500.0f);
                CheckQueenBallRing(&queenBga[2], 484, (QVec *)m[3], &objPos, r);
            }
        }
    }
    if (hit) {
        GOBJ_SUB(g)->disp = 0;
    } else {
        GOBJ_SUB(g)->disp = 1;
    }
    if (w->attacked != 0) {
        w->attacked = 0;
        w->live = 0;
        pbga_start(&w->bga, 479);
        _CopyVector(w->bga->pos, m[3]);
        CopyQuaternion(w->bga->rot, IdentityQuaternion);
        ExecuteSEPackage(g, 94);
    }
    if (w->cancel != 0) {
        w->cancel = 0;
        w->busy = 0;
        w->live = 0;
    }
    if (w->live != 0) {
        UnitMatrix33((QMat3 *)m);
        scale_m34((LVec *)m, m, w->scale);
        CopyMatrix((void *)GOBJ_SUB(g)->nodeMtx, m);
        if (hit == 0 && w->hit == 0 && _AttackCenter(g, 16, m[3], 0, r, 0) != 0) {
            w->hit = 1;
            if (weapon != 0) {
                ExecuteSEPackage(boyGObj, 97);
            } else {
                ExecuteSEPackage(boyGObj, 91);
            }
        }
        if (r > 5000.0f) {
            w->busy = 0;
            w->live = 0;
        }
        {
            /* per-barrier-index growth rate, one table per stage */
            const float *rate;

            if (stage_no == 37) {
                rate = &ballSpeedRateSt25[num];
            } else {
                rate = &ballSpeedRateDefault[num];
            }
            w->scale += *rate;
        }
    } else {
        m[3][0] = 4294967296.0f;
        m[3][1] = 4294967296.0f;
        m[3][2] = 4294967296.0f;
        m[3][3] = 0.0f;
        CopyMatrix((void *)GOBJ_SUB(g)->nodeMtx, m);
    }
}

void QueenBallDL(GObj *g)
{
    QVec ballPos;
    QVec selfPos;
    QVec queenPos;
    QueenBallWork *w;
    BgaPlayNode *o;
    BgaPlayNode **q;
    int i;

    w = GOBJ_SUB(g)->work;
    if (w->live != 0) {
        GetRootPosition(ballPos.f, g);
        SetupDarkVolume(&ballPos, w->scale * 100.0f, 10.0f);
        p2o_SetDefaultEnviroment();
        p2o_DispVU1Default(g);
    }
    if (w->bga != 0) {
        stage_SetScale(479, w->scale);
        if (stage_DispBgAnimation(&w->bga) != 0) {
            Act *act;

            w->bga = 0;
            w->busy = 0;
            act = GOBJ_ACT(g);
            act->attacker = 0;
            act->hit = 0;
        }
    }
    GetRootPosition(selfPos.f, g);
    GetRootPosition(queenPos.f, boyGObj);
    q = queenBga;
    for (i = 0; i < 4; i++, q++) {
        if ((o = *q) != 0) {
            long long id = *(long long *)o & 0x3FFF;

            if (id == 483 || id == 485) {
                SetQueenBallOrient(o, &selfPos, &queenPos);
            }
            if (stage_DispBgAnimation(q) != 0) {
                *q = 0;
            }
        }
    }
}

void actQueenStart(GObj *g)
{
    Act *sub = actInitialize(g);

    actInitialize_ext_charcter(g);
    _ACTWait(1);
    actCreateSubThread(subQueenBrainMain, 20);
    actCreateSubThread(subQueenControl, 21);
    actCreateSubThread(gene_enemy, 21);
    ICO_RAW(QueenVal, sub, 0x130, *(QueenVal *)&sub->motReq).motReq =
        SetMotionRequest(g, 270, sub->env.motOriReq);
    GOBJ_SUB(g)->cylinderOn = 1;
}

void QueenStartAttack(void)
{
    GObj *g;

    g = isysGObjSearchFromObjKindID_begin(47);
    ((QueenWork *)GOBJ_SUB(g)->work)->st.f.attack = 1;

    g = isysGObjSearchFromObjKindID_begin(54);
    while (g != 0) {
        ((QueenBarrierWork *)GOBJ_SUB(g)->work)->active = 1;
        g = isysGObjSearchFromObjKindID_next(g);
    }
}

int QueenInqDead(void)
{
    GObj *g = isysGObjSearchFromObjKindID_begin(47);
    return ((QueenWork *)GOBJ_SUB(g)->work)->st.f.dead;
}

int QueenBoysWeaponPower(void)
{
    GObj *g = isysGObjSearchFromObjKindID_begin(47);
    return ((QueenWork *)GOBJ_SUB(g)->work)->power;
}

float QueenBarrierRadius(GObj *gobj)
{
    return ((QueenBarrierWork *)GOBJ_SUB(gobj)->work)->radius;
}

int QueenBarrierInqBreakable(void)
{
    QueenWork *b;
    int ret = 0;

    b = GOBJ_SUB(isysGObjSearchFromObjKindID_begin(47))->work;
    if (b->power > 0 || b->wait > 0) {
        ret = 1;
    }
    return ret;
}

void queenBarrierBeforeFunc(GObj *g)
{
    GObjMailQueue *q = (GObjMailQueue *)&g->mailBox;
    QueenBarrierWork *w = GOBJ_SUB(g)->work;
    char *other;
    int i;

    for (i = 0; i < q->num; i++) {
        GObjMailEntry *e = &q->e[i];

        if (e->mail == 13) {
            debug_StdPrintfDummy(queenBarrierAttackedMsg);
            w->hit = 1;
            w->react = 1;
            other = isysGObjSearchFromObjKindID_begin(53);
            if (other != 0) {
                ((QueenBallWork *)GOBJ_SUB(other)->work)->cancel = 1;
            }
            queen_barrier_set_damage();
        }
    }
    q->num = 0;
}

int InqQueenBarrierExist(void)
{
    GObj *g;
    int exist = 0;

    g = isysGObjSearchFromObjKindID_begin(54);
    if (g != 0) {
        exist = ((QueenBarrierWork *)GOBJ_SUB(g)->work)->damage < 5;
    }
    return exist;
}

void *InitQueenBarrierGeo(GObj *g)
{
    QueenBarrierWork *w;

    Sub15C *ext = GOBJ_SUB(g);

    w = iosMallocDebug(ios_partition_sugipon, sizeof(QueenBarrierWork), queenFile, 991);
    memset(w, 0, sizeof(QueenBarrierWork));
    ext->work = w;
    w->radius = 300.0f;
    GetRootPosition(w->pos, g);
    actInitialize(g);
    actInitialize_ext_charcter(g);
    queen_barrier_disp_init();
    return w;
}

float QueenBallRadius(GObj *gobj)
{
    return ((QueenBallWork *)GOBJ_SUB(gobj)->work)->scale * 100.0f;
}

float GetQueenBallThickness(void)
{
    return 150.0f;
}

void queenBallBeforeFunc(GObj *g)
{
    GObjMailQueue *q = (GObjMailQueue *)&g->mailBox;
    QueenBallWork *w = GOBJ_SUB(g)->work;
    int i;

    for (i = 0; i < q->num; i++) {
        GObjMailEntry *e = &q->e[i];

        if (e->mail != 13) {
            debug_StdPrintfDummy(mailFmt, e->mail);
        } else if (scpGameStat_BoyWeaponkind() == 5) {
            debug_StdPrintfDummy(queenBallAttackedMsg);
            w->attacked = 1;
            iosOmSendMail(boyGObj, 425, g);
        }
    }
    q->num = 0;
}

void *InitQueenBallGeo(GObj *g)
{
    QueenBallWork *w;

    Sub15C *ext = GOBJ_SUB(g);

    w = iosMallocDebug(ios_partition_sugipon, sizeof(QueenBallWork), queenFile, 1284);
    ext->work = w;
    memset(w, 0, sizeof(QueenBallWork));
    w->scale = 0.0f;
    GetRootPosition(w->pos, g);
    actInitialize(g);
    actInitialize_ext_charcter(g);
    return w;
}

void subQueenControl(volatile ICO_WORD g)
{
    QueenWork *w = GOBJ_SUB(g)->work;

    _ACTWait(1);
    for (;;) {
        if (w->st.f.pause == 0) {
            Debug_StickControl((GObj *)g);
        }
        _ACTWait(1);
    }
}
