/*
 * port/game/test/torch_work_test.c
 *
 * The torch record every torch starts from on each disc
 * (ico2/sugipon/src/torch.c emptyTorchWork, the file linked whole; what it
 * names from the rest of the game is stubbed below), played through the
 * file's own InitTorchGeo, TorchGeo and LightTorchOff.  Issue 160: the
 * 2001 builds' record (flags, lightOn, burnTime, life 65536, chainFlag,
 * effect[5] all -1: the North American disc's .data at 0x4C6218, the 40
 * bytes 0 0 0 0x10000 0 -1 -1 -1 -1 -1) was written positionally into the
 * host's later layout, so it landed with life 0 and every effect slot 0: a
 * torch lit at its layout went out on its second frame and deleted
 * particle effect 0 (a candle's flame, or any other effect) as it went.
 *
 *   torch_work_test [pal|jpn|kor|cn|ntsc|aug6|jun26]   (default pal)
 *
 * Checks, for the selected disc: a torch lit at its layout (layout word 1)
 * stays lit for 600 frames (life 65536 on every disc) and runs the four
 * flame effects the default type makes; LightTorchOff deletes exactly
 * those four and no effect it did not make; an unlit torch (layout word 0,
 * a stick) stays unlit, and LightTorchOff on it deletes nothing; a torch
 * given SetTorchLife(10) goes out after 11 frames.  CPU only.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../core/region.h"
#include "typedef.h"
#include "sceneManager.h"
#include "torch.h"

struct IosMemPart;

static int failures;

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            failures++;                                                                            \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* --- the particle effects torch.c makes and deletes ------------------------ */

#define EFFECT_IDS 128
/* the ids start past 0 so a delete of id 0 (the 2001 record's slot value)
   is always one the torch never made */
#define EFFECT_FIRST 10
static int effect_live[EFFECT_IDS];
static int effect_next = EFFECT_FIRST;
static int effect_made;
static int effect_deleted;
static int effect_foreign; /* deletes of an id that is not a live effect */

static int make_effect(void)
{
    int id = effect_next++;

    if (id >= EFFECT_IDS) {
        return -1;
    }
    effect_live[id] = 1;
    effect_made++;
    return id;
}

int SetParticleEffect(int no, void *pos, void *quat);

int SetParticleEffect(int no, void *pos, void *quat)
{
    (void)no, (void)pos, (void)quat;
    return make_effect();
}

int SetParticleEffectByPartition(int no, void *pos, void *quat, struct IosMemPart *part);

int SetParticleEffectByPartition(int no, void *pos, void *quat, struct IosMemPart *part)
{
    (void)no, (void)pos, (void)quat, (void)part;
    return make_effect();
}

int SetParticleEffectActiveSensing(int no, void *pos, void *quat);

int SetParticleEffectActiveSensing(int no, void *pos, void *quat)
{
    (void)no, (void)pos, (void)quat;
    return make_effect();
}

void DeleteParticleEffect(int no);

void DeleteParticleEffect(int no)
{
    if (no < 0 || no >= EFFECT_IDS || !effect_live[no]) {
        effect_foreign++;
        return;
    }
    effect_live[no] = 0;
    effect_deleted++;
}

void SetParticleEffectGeometry(int id, void *pos, void *quat);

void SetParticleEffectGeometry(int id, void *pos, void *quat)
{
    (void)id, (void)pos, (void)quat;
}

void SetParticleEffectPauseFlag(int id, int pause);

void SetParticleEffectPauseFlag(int id, int pause)
{
    (void)id, (void)pause;
}

void SetParticleEffectDrainLevel(int id, float level);

void SetParticleEffectDrainLevel(int id, float level)
{
    (void)id, (void)level;
}

void ExecParticleEffect(int no);

void ExecParticleEffect(int no)
{
    (void)no;
}

/* --- the rest of the game torch.c names ------------------------------------ */

float IdentityQuaternion[4] = {0.0f, 0.0f, 0.0f, 1.0f};
struct IosMemPart *ios_partition_seki;
struct IosMemPart *ios_partition_sugipon;
GObj *boyGObj;
GObj *girlGObj;

void *iosMallocDebug(struct IosMemPart *part, int size, const char *file, int line);

void *iosMallocDebug(struct IosMemPart *part, int size, const char *file, int line)
{
    (void)part, (void)file, (void)line;
    return calloc(1, (size_t)size);
}

void debug_StdPrintfDummy(const char *fmt, ...);

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void GetRootPosition(void *pos, struct GObj *obj);

void GetRootPosition(void *pos, struct GObj *obj)
{
    (void)obj;
    memset(pos, 0, 16);
}

void CopyVector(void *dst, void *src);

void CopyVector(void *dst, void *src)
{
    memcpy(dst, src, 16);
}

void sceVu0SubVector(void *out, void *a, void *b);

void sceVu0SubVector(void *out, void *a, void *b)
{
    float r[4];
    int i;

    for (i = 0; i < 4; i++) {
        r[i] = ((float *)a)[i] - ((float *)b)[i];
    }
    memcpy(out, r, sizeof r);
}

void sceVu0UnitMatrix(void *m);

void sceVu0UnitMatrix(void *m)
{
    float *f = m;
    int i;

    memset(f, 0, 64);
    for (i = 0; i < 4; i++) {
        f[i * 5] = 1.0f;
    }
}

void StopSEPackage(struct GObj *gobj);

void StopSEPackage(struct GObj *gobj)
{
    (void)gobj;
}

void ExecuteSEPackage(struct GObj *gobj, int id);

void ExecuteSEPackage(struct GObj *gobj, int id)
{
    (void)gobj, (void)id;
}

unsigned char ACTGame_NoWeapon(struct GObj *self);

unsigned char ACTGame_NoWeapon(struct GObj *self)
{
    (void)self;
    return 1;
}

/* the kind-10 search: the torches made by this test */
#define TORCHES 4
static GObj *torches[TORCHES];

void *isysGObjSearchFromObjKindID_begin(int kind);

void *isysGObjSearchFromObjKindID_begin(int kind)
{
    (void)kind;
    return torches[0];
}

void *isysGObjSearchFromObjKindID_next(void *o);

void *isysGObjSearchFromObjKindID_next(void *o)
{
    int i;

    for (i = 0; i + 1 < TORCHES; i++) {
        if (torches[i] == o) {
            return torches[i + 1];
        }
    }
    return 0;
}

void ico_host_torch_out(int burnTime, int life, int flags);

void ico_host_torch_out(int burnTime, int life, int flags)
{
    (void)burnTime, (void)life, (void)flags;
}

void ico_host_torch_caught(int flags);

void ico_host_torch_caught(int flags)
{
    (void)flags;
}

/* --- the test --------------------------------------------------------------- */

static GObj *new_torch(int slot, int lit)
{
    GObj *g = calloc(1, sizeof(GObj));
    Sub15C *s = calloc(1, sizeof(Sub15C));
    SObjSimpleSetting lay;

    g->dobj = s;
    g->active = 1;
    torches[slot] = g;
    memset(&lay, 0, sizeof lay);
    lay.obj = lit;
    InitTorchGeo(g, &lay);
    return g;
}

int main(int argc, char **argv)
{
    const char *slug = argc > 1 ? argv[1] : "pal";
    GObj *lit;
    GObj *stick;
    GObj *timed;
    int made;
    int i;

    if (ico_region_select(slug) != 0) {
        printf("FAIL: no disc \"%s\"\n", slug);
        return 1;
    }

    /* a torch lit at its layout: the default type's four flame effects */
    lit = new_torch(0, 1);
    made = effect_made;
    CHECK(IsTorchLightOn(lit) == 1, "%s: the lit torch is not lit after InitTorchGeo", slug);
    CHECK(made == 4, "%s: the lit torch made %d effects, not 4", slug, made);
    for (i = 0; i < 600; i++) {
        TorchGeo(lit);
        if (!IsTorchLightOn(lit)) {
            break;
        }
    }
    CHECK(i == 600, "%s: the lit torch went out on frame %d (life 65536 expected)", slug, i + 1);
    CHECK(effect_foreign == 0, "%s: %d deletes of effects the torch never made while burning", slug,
          effect_foreign);

    /* an unlit torch (a stick): stays unlit, makes and deletes nothing */
    stick = new_torch(1, 0);
    CHECK(IsTorchLightOn(stick) == 0, "%s: the unlit torch is lit", slug);
    for (i = 0; i < 10; i++) {
        TorchGeo(stick);
    }
    CHECK(IsTorchLightOn(stick) == 0, "%s: the unlit torch lit itself", slug);
    LightTorchOff(stick);
    CHECK(effect_made == made && effect_deleted == 0 && effect_foreign == 0,
          "%s: the unlit torch made %d / deleted %d / foreign %d", slug, effect_made - made,
          effect_deleted, effect_foreign);

    /* putting the lit torch out deletes its own four effects, nothing else */
    LightTorchOff(lit);
    CHECK(IsTorchLightOn(lit) == 0, "%s: LightTorchOff left the torch lit", slug);
    CHECK(effect_deleted == 4, "%s: LightTorchOff deleted %d effects, not 4", slug, effect_deleted);
    CHECK(effect_foreign == 0, "%s: LightTorchOff deleted %d effects it never made", slug,
          effect_foreign);

    /* a torch given a life of 10 frames burns 10 and goes out on the 11th
       (life < burnTime) */
    timed = new_torch(2, 1);
    SetTorchLife(timed, 10, 5);
    for (i = 0; i < 20 && IsTorchLightOn(timed); i++) {
        TorchGeo(timed);
    }
    CHECK(i == 11, "%s: the 10-frame torch went out after %d frames, not 11", slug, i);
    CHECK(effect_foreign == 0, "%s: the 10-frame torch deleted %d effects it never made", slug,
          effect_foreign);

    if (failures == 0) {
        printf("torch_work %s: ok\n", slug);
    }
    return failures != 0;
}
