#include "itou_boss.h"
#include "debug.h"
#include "memory.h"
#include "gobj.h"
#include "act-game.h"
#include "act.h"
#include "enemy_act.h"
#include "script.h"
#include "Matrix.h"
#include "StageAnimation.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "quaternion.h"
#include "generator.h"
#include "sugiCommon.h"
#include <string.h>
#include <libvu0.h>
#include "main.h"
#include "ios.h"
#include "particleEffect.h"
#include "gather_effect.h"
#include "itou_sub.h"
#include "itou_common.h"

/* One capsule: its BGA, its state (0 closed, 1 gathering, 2 open), the
   placement InitBossCtrlGeo gives it, its release point, and whether a
   gene_enemy thread is releasing from it. */
typedef struct {        /* field names derived */
    BgaPlayNode *bga;   /* 0x00 */
    signed char state;  /* 0x04 */
    sceVu0FVECTOR quat; /* 0x10 */
    sceVu0FVECTOR pos;  /* 0x20 */
    float *release;     /* 0x30, a row of capsuleRelease */
    signed char busy;   /* 0x34 */
} CapsuleRec;           /* derived name */

/* The boss flags, whose first byte is the capsule-ghost stage flag, the
   fifty-three capsules, and the gene_enemy threads' done flags, which each
   thread polls while its gather effect's end callback sets it.
   itou_boss_gflag_init clears the flags and the capsules together. */
static signed char gflag[16]; /* derived name */

static CapsuleRec capsule[53]; /* derived name */

static volatile int geneDone[16]; /* derived name */

/* The fifty-three capsules' placements and the points their enemies are
   released at.  InitBossCtrlGeo copies each position and turns each rotation
   into the record's quaternion, and gene_enemy puts a released enemy at the
   capsule's release point. */
typedef struct {     /* field names derived */
    float rot[3][4]; /* the rotation rows, w zero */
    float pos[4];
} CapsulePlace; /* derived name */

static const CapsulePlace capsulePlace[53] = {
    /* derived name */
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {1100.0f, -763.8f, 1641.22f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {800.0f, -763.8f, 1641.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {1100.0f, -363.8f, 1441.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {800.0f, -363.8f, 1441.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {499.998f, -763.8f, 1641.22f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {199.998f, -763.8f, 1641.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {499.998f, -363.8f, 1441.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {199.998f, -363.8f, 1441.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-100.002f, -763.8f, 1641.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-100.002f, -363.8f, 1441.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-399.911f, -763.8f, 1641.22f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-699.91f, -763.8f, 1641.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-399.911f, -363.8f, 1441.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-699.91f, -363.8f, 1441.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-999.913f, -763.8f, 1641.22f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-1299.91f, -763.8f, 1641.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-999.913f, -363.8f, 1441.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-1299.91f, -363.8f, 1441.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-1599.91f, -363.8f, 1441.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {1100.1f, -763.8f, -1641.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-1300.1f, -763.8f, -1641.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-1600.1f, -363.8f, -1441.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-1300.1f, -363.8f, -1441.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-1000.1f, -763.8f, -1641.22f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-700.097f, -763.8f, -1641.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-700.097f, -363.8f, -1441.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-400.097f, -763.8f, -1641.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-400.097f, -363.8f, -1441.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-100.188f, -763.8f, -1641.22f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {199.811f, -763.8f, -1641.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-100.188f, -363.8f, -1441.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {199.811f, -363.8f, -1441.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {499.814f, -763.8f, -1641.22f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {799.811f, -763.8f, -1641.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {499.814f, -363.8f, -1441.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {799.811f, -363.8f, -1441.2f, 1.0f}},
    {{{-1.0f, 0.0f, 1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {1099.81f, -363.8f, -1441.2f, 1.0f}},
    {{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}},
     {-1894.99f, -763.8f, 1884.19f, 1.0f}},
    {{{0.987688f, 0.0f, 0.156434f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.156434f, 0.0f, 0.987688f, 0.0f}},
     {-2187.96f, -763.8f, 1838.31f, 1.0f}},
    {{{0.951057f, 0.0f, 0.309017f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.309017f, 0.0f, 0.951057f, 0.0f}},
     {-2470.14f, -763.8f, 1747.17f, 1.0f}},
    {{{0.891007f, 0.0f, 0.45399f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.45399f, 0.0f, 0.891007f, 0.0f}},
     {-2734.59f, -763.8f, 1613.0f, 1.0f}},
    {{{0.809017f, 0.0f, 0.587785f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.587785f, 0.0f, 0.809017f, 0.0f}},
     {-2974.8f, -763.8f, 1439.12f, 1.0f}},
    {{{0.707107f, 0.0f, 0.707107f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.707107f, 0.0f, 0.707107f, 0.0f}},
     {-3184.85f, -763.8f, 1229.8f, 1.0f}},
    {{{0.587785f, 0.0f, 0.809017f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.809017f, 0.0f, 0.587785f, 0.0f}},
     {-3359.56f, -763.8f, 990.199f, 1.0f}},
    {{{0.45399f, 0.0f, 0.891007f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.891007f, 0.0f, 0.45399f, 0.0f}},
     {-3494.65f, -763.8f, 726.217f, 1.0f}},
    {{{-1.0f, 0.0f, -1.22461e-16f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {1.22461e-16f, 0.0f, -1.0f, 0.0f}},
     {-1895.0f, -763.8f, -1884.2f, 1.0f}},
    {{{-0.987688f, 0.0f, 0.156434f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.156434f, 0.0f, -0.987688f, 0.0f}},
     {-2187.97f, -763.8f, -1838.32f, 1.0f}},
    {{{-0.951057f, 0.0f, 0.309017f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.309017f, 0.0f, -0.951057f, 0.0f}},
     {-2470.15f, -763.8f, -1747.17f, 1.0f}},
    {{{-0.891007f, 0.0f, 0.45399f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.45399f, 0.0f, -0.891007f, 0.0f}},
     {-2734.6f, -763.8f, -1613.01f, 1.0f}},
    {{{-0.809017f, 0.0f, 0.587785f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.587785f, 0.0f, -0.809017f, 0.0f}},
     {-2974.81f, -763.8f, -1439.12f, 1.0f}},
    {{{-0.707107f, 0.0f, 0.707107f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.707107f, 0.0f, -0.707107f, 0.0f}},
     {-3184.86f, -763.8f, -1229.8f, 1.0f}},
    {{{-0.587785f, 0.0f, 0.809017f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.809017f, 0.0f, -0.587785f, 0.0f}},
     {-3359.58f, -763.8f, -990.197f, 1.0f}},
    {{{-0.45399f, 0.0f, 0.891007f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {-0.891007f, 0.0f, -0.45399f, 0.0f}},
     {-3494.66f, -763.8f, -726.213f, 1.0f}},
};

static const float capsuleRelease[53][4] = {
    /* derived name */
    {1100.0f, -601.8f, 1554.22f, 1.0f},    {800.0f, -601.8f, 1554.2f, 1.0f},
    {1100.0f, -201.8f, 1354.2f, 1.0f},     {800.0f, -201.8f, 1354.2f, 1.0f},
    {499.998f, -601.8f, 1554.22f, 1.0f},   {199.998f, -601.8f, 1554.2f, 1.0f},
    {499.998f, -201.8f, 1354.2f, 1.0f},    {199.998f, -201.8f, 1354.2f, 1.0f},
    {-100.002f, -601.8f, 1554.2f, 1.0f},   {-100.002f, -201.8f, 1354.2f, 1.0f},
    {-399.911f, -601.8f, 1554.22f, 1.0f},  {-699.91f, -601.8f, 1554.2f, 1.0f},
    {-399.911f, -201.8f, 1354.2f, 1.0f},   {-699.91f, -201.8f, 1354.2f, 1.0f},
    {-999.913f, -601.8f, 1554.22f, 1.0f},  {-1299.91f, -601.8f, 1554.2f, 1.0f},
    {-999.913f, -201.8f, 1354.2f, 1.0f},   {-1299.91f, -201.8f, 1354.2f, 1.0f},
    {-1599.91f, -201.8f, 1354.2f, 1.0f},   {1100.1f, -601.8f, -1554.2f, 1.0f},
    {-1300.1f, -601.8f, -1554.2f, 1.0f},   {-1600.1f, -201.8f, -1354.2f, 1.0f},
    {-1300.1f, -201.8f, -1354.2f, 1.0f},   {-1000.1f, -601.8f, -1554.22f, 1.0f},
    {-700.097f, -601.8f, -1554.2f, 1.0f},  {-700.097f, -201.8f, -1354.2f, 1.0f},
    {-400.097f, -601.8f, -1554.2f, 1.0f},  {-400.097f, -201.8f, -1354.2f, 1.0f},
    {-100.188f, -601.8f, -1554.22f, 1.0f}, {199.811f, -601.8f, -1554.2f, 1.0f},
    {-100.188f, -201.8f, -1354.2f, 1.0f},  {199.811f, -201.8f, -1354.2f, 1.0f},
    {499.814f, -601.8f, -1554.22f, 1.0f},  {799.811f, -601.8f, -1554.2f, 1.0f},
    {499.814f, -201.8f, -1354.2f, 1.0f},   {799.811f, -201.8f, -1354.2f, 1.0f},
    {1099.81f, -201.8f, -1354.2f, 1.0f},   {-1894.99f, -601.8f, 1797.19f, 1.0f},
    {-2174.35f, -601.8f, 1752.38f, 1.0f},  {-2443.26f, -601.8f, 1664.43f, 1.0f},
    {-2695.09f, -601.8f, 1535.48f, 1.0f},  {-2923.66f, -601.8f, 1368.74f, 1.0f},
    {-3123.33f, -601.8f, 1168.28f, 1.0f},  {-3289.18f, -601.8f, 939.062f, 1.0f},
    {-3417.13f, -601.8f, 686.72f, 1.0f},   {-1895.0f, -601.8f, -1797.2f, 1.0f},
    {-2174.36f, -601.8f, -1752.39f, 1.0f}, {-2443.27f, -601.8f, -1664.43f, 1.0f},
    {-2695.1f, -601.8f, -1535.49f, 1.0f},  {-2923.67f, -601.8f, -1368.74f, 1.0f},
    {-3123.34f, -601.8f, -1168.28f, 1.0f}, {-3289.2f, -601.8f, -939.06f, 1.0f},
    {-3417.14f, -601.8f, -686.716f, 1.0f},
};

static void effect_end_func(int id)
{
    CapsuleRec *e;

    if (isysGObjSearchFromObjKindID_begin(65) != 0) {
        e = &capsule[GetParticleEffectData(id)->user.capsule];
        pbga_start(&e->bga, 552);
        _CopyVector(e->bga->pos, e->pos);
        CopyQuaternion(e->bga->rot, e->quat);
        e->state = 2;
        ExecuteSEPackage(0, 101);
    }
}

void bossCtrlBeforeFunc(GObj *self)
{
    int buf[53];
    float pos[4];
    GObjMailQueue *q;
    GObjMailEntry *e;
    int idx;
    CapsuleRec *e2;
    int i;
    unsigned int j;
    int cnt;
    int r;

    q = (GObjMailQueue *)&self->mailBox;
    for (i = 0; i < q->num; i++) {
        e = &q->e[i];
        if (e->mail == 18) {
            if (e->data != 0) {
                cnt = 0;
                for (j = 0; j < 53; j++) {
                    if (capsule[j].state == 0) {
                        buf[cnt++] = j;
                    }
                }
                if (cnt > 0) {
                    idx = buf[(int)(random_unit() * cnt)];
                    e2 = &capsule[idx];
                    GetRootPosition(pos, e->data);
                    r = GatherEffect_Set(12, pos, IdentityQuaternion, e2->pos, 1.0f,
                                         effect_end_func);
                    if (r >= 0) {
                        GetParticleEffectData(r)->user.capsule = idx;
                        e2->state = 1;
                    }
                }
            }
            ExecuteSEPackage(self, 100);
        }
    }
    q->num = 0;
}

inline int InqCapsuleGhostBossStage(void)
{
    int r = 0;
    if (stage_no == 86 || stage_no == 3 || stage_no == 46)
        r = 1;
    return r;
}

void BossEnemyFunc(void *self)
{
    if (gflag[0] != 0 && InqCapsuleGhostBossStage() != 0) {
        _ACTSetEnemyDisappearSpeed(self, 6.0f);

        switch (GOBJ_SUB(self)->ctrl.motion) {
        default:
            break;
        case 905:
            ACTGame_SetMotionPlaySpeedRatio_Reserve(self, 2.0f, 0);
            break;
        case 955:
        case 956:
        case 957:
            ACTGame_SetMotionPlaySpeedRatio_Reserve(self, 2.0f, 0);
            break;
        case 927:
        case 928:
        case 929:
            ACTGame_SetMotionPlaySpeedRatio_Reserve(self, 1.2f, 0);
            break;
        }
    }
}

/* the number of gene_enemy threads started, which indexes their done
   flags, and the number of releases under way */
static int geneCount; /* derived name */

static int geneReleasing; /* derived name */

/* send an enemy off-world and clear its live flag */
static inline void sendEnemyAway(GObj *o) /* derived name */
{
    float pos[4];
    pos[2] = pos[1] = pos[0] = 4294967296.0f;
    pos[3] = 0.0f;
    SetRootPosition(o, pos);
    GOBJ_SUB(o)->disp = 0;
}

/* drop an enemy at a position and mark its actor live */
static inline void putEnemyAt(GObj *o, float *pos) /* derived name */
{
    SetRootPosition(o, pos);
    GOBJ_SUB(o)->disp = 1;
}

/* The DEBUG build's switch to release the enemies without their gather
   effect; retail builds it as 0. */
#ifdef DEBUG

extern int geneDebugNoEffect; /* derived name */

#define GENE_DEBUG_NO_EFFECT geneDebugNoEffect
#else
#define GENE_DEBUG_NO_EFFECT 0
#endif

static inline void gene_eff_end_func(int id);

/* this file's own gene_enemy; queen.c defines a global of the same name */
static void gene_enemy(volatile ICO_WORD gobj)
{
    int no = geneCount;
    volatile int *flag = &geneDone[geneCount++];
    CapsuleRec *buf[53];
    GObj *o;
    void *c;
    CapsuleRec *p;
    int i;
    unsigned int j;
    int total;
    int alive;
    int freen;
    int num;

    _ACTWait(1);

    i = 0;
    for (o = isysGObjSearchFromObjKindID_begin(33); o != 0;
         o = isysGObjSearchFromObjKindID_next(o), i++) {
        if (i == no) {
            break;
        }
    }

    for (;; _ACTWait(1)) {
        total = 0;
        alive = 0;
        if (stage_no == 86) {
            if ((pad[0].flags & 0x40) != 0) {
                gflag[0] = 1;
            }
        }
        if (gflag[0] != 0) {
            c = isysGObjSearchFromObjKindID_begin(4);
            while (c != 0) {
                total++;
                if (isEnemyHyde(c) == 0) {
                    alive++;
                }
                c = isysGObjSearchFromObjKindID_next(c);
            }
            if ((debug_font_flag & 1) != 0) {
                debug_Printf(10, 90, 0xFFFFFFFF, "enemy %d/%d\n", alive, total);
            }
            num = 0;
            freen = 0;
            for (j = 0; j < 53; j++) {
                p = &capsule[j];
                if (p->state <= 0 && p->busy == 0) {
                    buf[num++] = p;
                }
                if (p->state <= 0) {
                    freen++;
                }
            }
            if (total >= freen) {
                total = freen;
            }
            if (alive + geneReleasing < total && num > 0 && o != 0) {
                float pp[4];
                float pos[4];
                float dir[4];
                float m[4][4];
                CapsuleRec *sel;
                /* r starts as "no effect" (-1): the DEBUG build can release the
                   enemy without its gather effect, and then the r >= 0 block is
                   skipped.  Retail builds the switch as 0, so the call always
                   sets r. */
                int r = -1;

                sel = buf[(int)(random_unit() * num)];
                sceVu0CopyVector(pos, sel->release);
                geneReleasing++;
                sel->busy = 1;
                *flag = 0;
                if (!GENE_DEBUG_NO_EFFECT) {
                    r = GatherEffect_Set(12, sel->pos, sel->quat, pos, 1.0f, gene_eff_end_func);
                }
                if (r >= 0) {
                    GetParticleEffectData(r)->user.done = flag;
                    SetParticleEffectClipEnableFlag(r, 0);
                    ExecuteSEPackage((GObj *)gobj, 99);
                    while (*flag == 0) {
                        _ACTWait(1);
                    }
                }
                putEnemyAt(o, pos);
                GetRootPosition(pp, boyGObj);
                sceVu0SubVector(dir, pp, pos);
                sceVu0Normalize(dir, dir);
                sceVu0UnitMatrix(m);
                m[1][0] = 0.0f;
                m[1][1] = 1.0f;
                m[1][2] = 0.0f;
                m[1][3] = 0.0f;
                sceVu0CopyVector(m[2], dir);
                sceVu0OuterProduct(m[0], m[1], m[2]);
                {
                    float q[4];
                    ico_m33_to_quat(q, m);
                    SetRootQuaternion(o, q);
                }
                Generator_Call(o);
                while (GeneratorWorkEnd(o) == 0) {
                    _ACTWait(1);
                }
                geneReleasing--;
                sel->busy = 0;
                _ACTWait(((60 - systemStatus[0] * 10) / systemStatus[1]) * 5);
                sendEnemyAway(o);
            }
        }
    }
}

void BossCtrlGeo(void *self)
{
    if (gflag[0] != 0)
        scpWakeupEnemyAll();
    else
        scpSleepEnemyAll();
}

/* the boss controller's actor start */
static inline void bossCtrlInit(void *gobj) /* derived name */
{
    actInitialize(gobj);
    _ACTWait(1);
    geneReleasing = 0;
}

inline void actBossCtrlStart(void *gobj)
{
    int no;
    GObj *o;
    int i;

    no = 0;
    bossCtrlInit(gobj);
    geneCount = 0;
    o = isysGObjSearchFromObjKindID_begin(33);
    while (o != 0) {
        sendEnemyAway(o);
        no++;
        o = isysGObjSearchFromObjKindID_next(o);
    }
    debug_StdPrintfDummy("n generator %d\n", no);
    for (i = 0; i < no; i++) {
        actCreateSubThread(gene_enemy, 21);
    }
}

inline ICO_WORD InitBossCtrlGeo(void *gobj)
{
    ICO_WORD ret;
    unsigned int k;
    CapsuleRec *base;
    CapsuleRec *e;
    char *m;
    char (*q_arr)[];
    char *q;
    char *r;

    ret = (ICO_WORD)iosMallocDebug(ios_partition_sugipon, 0, __FILE__, 350);
    actInitialize(gobj);
    actInitialize_ext_charcter(gobj);
    debug_StdPrintfDummy("N_CAPSULE %d\n", 53);

    base = capsule;
    m = (char *)base->pos;
    q_arr = (char (*)[])capsulePlace[0].pos;
    r = (char *)capsuleRelease;
    q = *q_arr;
    k = 0;
    do {
        e = &base[k];
        e->bga = 0;
        if (e->state == 1) {
            e->state = 2;
        }
        sceVu0CopyVector(m, q);
        ico_m33_to_quat(m - 16, q - 48);
        e->release = (float *)r;
        q += 64;
        m += 64;
        r += 16;
        k++;
    } while (k < 53);
    return ret;
}

void itou_boss_gflag_init(void)
{
    /* the EE's linker puts capsule right after gflag and one memset clears
       both; a host compiler may order the two (or pad between them) freely */
    memset(gflag, 0, sizeof(gflag));
    memset(capsule, 0, sizeof(capsule));
}

void BossCtrlDL(void)
{
    CapsuleRec *base;
    CapsuleRec *e;
    unsigned int k;
    int n;

    n = 0;
    base = capsule;
    for (k = 0; k < 53; k++) {
        e = &base[k];
        if (e->state >= 2) {
            if (stage_DispBgAnimation(&e->bga) != 0) {
                pbga_start(&e->bga, 553);
                _CopyVector(e->bga->pos, e->pos);
                CopyQuaternion(e->bga->rot, e->quat);
            }
        }
        if (e->state != 0) {
            n++;
        }
    }
    if ((debug_font_flag & 1) != 0) {
        debug_Printf(10, 60, 0xFFFFFFFF, "capsule %d/%d", n, 53);
    }
}

inline void CapsuleGhostBossStart(void)
{
    gflag[0] = 1;
}

inline int InqCapsuleGhostBossEnd(void)
{
    int no = 0;
    unsigned int cnt = 0;
    void *o;

    if (isysGObjSearchFromObjKindID_begin(65) != 0) {
        CapsuleRec *base = capsule;
        unsigned int i = 0;
        do {
            if (base[i].state >= 2) {
                cnt++;
            }
            i++;
        } while (i < 53);
    }
    o = isysGObjSearchFromObjKindID_begin(4);
    while (o != 0) {
        if (isEnemyHyde(o) == 0) {
            no++;
        }
        o = isysGObjSearchFromObjKindID_next(o);
    }
    return cnt >= 53 && no == 0;
}

static inline void gene_eff_end_func(int id)
{
    *GetParticleEffectData(id)->user.done = 1;
}
