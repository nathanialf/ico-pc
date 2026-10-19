/*
 * ico2/sugipon/include/item.h
 *
 * The declarations of what item.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ITEM_H
#define ITEM_H

struct GObj;

/* item-kind-def: one item kind's break animations, 0x20 bytes, seven kinds:
   the stage BG animations (972 for none, JPN 949) and their modes the item plays
   while it stays, breaks, drops and is hit, and whether the break plays SE
   package 43. */
typedef struct {        /* field names derived */
    int stayAnim;       /* 0x00 */
    int stayMode;       /* 0x04 */
    int breakAnim;      /* 0x08 */
    int breakMode;      /* 0x0C */
    int dropAnim;       /* 0x10 */
    int hitAnim;        /* 0x14 */
    int hitMode;        /* 0x18 */
    unsigned int flags; /* 0x1C, bit 0: play SE package 43 */
} ItemBreakRec;         /* derived name */

extern ItemBreakRec itemKind[];
int BreakItemFromOutside(struct GObj *gobj);
int BreakItemWithAttackHit(struct GObj *gobj, float *dir);
int CheckCarryableItem(struct GObj *gobj);
int CheckItemDead(struct GObj *gobj);
int GetCharHeldItem(struct GObj *chara);
int GetItemKind(struct GObj *gobj);
void HoldItem(struct GObj *gobj, struct GObj *holder);
int IsBombExplode(struct GObj *gobj);
void ReleaseItem(struct GObj *gobj);
int ReviveAllCarryableItems(void);
int ReviveAllCarryableItemsWithNonSleepFrame(int nonSleepFrame);
int ReviveAllCarryableItemsWithRandomVelocity(float up, float horz);
int ReviveCarryableItemsWithBoundary(void *center, float radius);
void ThrowItem(struct GObj *gobj, void *vel);

struct ItemLayout;

char *InitItemGeo(struct GObj *gobj, struct ItemLayout *layout);
void ItemGeo(struct GObj *gobj);
void ItemDL(struct GObj *gobj);
int IsItemHoldable(struct GObj *gobj);
void *GetBombTorchGObj(struct GObj *item);
void StopItemExplodeAnimationAll(void);

#endif /* ITEM_H */
