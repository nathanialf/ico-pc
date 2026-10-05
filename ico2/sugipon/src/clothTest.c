#include "memory.h"
#include "clothAnimation.h"
#include "ios.h"

/* the twenty anchor records of the test cloth, then the InitClothes config
   array, a list of 0x1C-byte records ended by one whose first field is -1 */
typedef struct ClothTestAnchor { /* field names derived */
    int parent;                  /* 0x00 */
    float len;                   /* 0x04 */
    char pad08[8];
    float pos[4];  /* 0x10 */
    float vel[4];  /* 0x20 */
} ClothTestAnchor; /* derived name */

static ClothTestAnchor clothTestAnchors[20] = {
    {-1, 15.0f, {0}, {0.0f, 0.0f, 15.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {-10.0f, 0.0f, 14.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {-20.0f, 0.0f, 14.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {-30.0f, 0.0f, 13.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {-40.0f, 0.0f, 10.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {-40.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {-40.0f, 0.0f, -10.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {-30.0f, 0.0f, -13.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {-20.0f, 0.0f, -14.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {-10.0f, 0.0f, -14.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {0.0f, 0.0f, -15.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {10.0f, 0.0f, -14.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {20.0f, 0.0f, -14.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {30.0f, 0.0f, -13.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {40.0f, 0.0f, -10.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {40.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {40.0f, 0.0f, 10.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {30.0f, 0.0f, 13.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {20.0f, 0.0f, 14.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
    {-1, 15.0f, {0}, {10.0f, 0.0f, 14.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 0.0f}},
}; /* derived name */

static ClothCfg clothTestCfg[2] = {{20, 20.0f, 15, 1, clothTestAnchors, 0, 3.0f},
                                   {-1}}; /* derived name */

ClothSet **InitClothTestGeo(void)
{
    ClothSet **p =
        iosMallocDebug(ios_partition_sugipon, 164 * sizeof(ClothSet *), "src/clothTest.c", 65);
    *p = InitClothes(clothTestCfg);
    return p;
}

void ClothTestGeo(void) {}

void ClothTestDL(void) {}
