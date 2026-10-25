#include "st04r.h"
#include "st04l.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "gobj_process.h"
#include "obj_manager.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act-game.h"
#include "act.h"
#include "commonact.h"
#include "girl_act.h"
#include "way_llf.h"
#include "camera-root.h"
#include "fightSound.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "attackCheckBoundary.h"
#include "item.h"
#include <libvu0.h>
#include "typedef.h"
#include "script.h"
#include "main.h"

static void actSt04rBrg1Sub(GObj *volatile self);
static void openGate(struct GObj *self);

static ActMail c1_ball_main_mes_pal[2] = {{406, actSt04rC1BallSwitch}, {429}}; /* derived name */

static ActMail c1_ball_main_mes_ntsc[2] = {{376, actSt04rC1BallSwitch}, {396}}; /* derived name */

static ActMail c1_ball_main_mes_aug6[2] = {{372, actSt04rC1BallSwitch}, {392}}; /* derived name */

static ActMail c1_ball_main_mes_jun26[2] = {{356, actSt04rC1BallSwitch}, {376}}; /* derived name */

#define c1_ball_main_mes                                                                           \
    PAL_NTSC_AUG6_JUN26(c1_ball_main_mes_pal, c1_ball_main_mes_ntsc, c1_ball_main_mes_aug6,        \
                        c1_ball_main_mes_jun26)

static ActMail c1_ball_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c1_ball_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c1_ball_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail c1_ball_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define c1_ball_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(c1_ball_mes_pal, c1_ball_mes_ntsc, c1_ball_mes_aug6, c1_ball_mes_jun26)

static ActMail c1_ball_switch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c1_ball_switch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c1_ball_switch_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail c1_ball_switch_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define c1_ball_switch_mes                                                                         \
    PAL_NTSC_AUG6_JUN26(c1_ball_switch_mes_pal, c1_ball_switch_mes_ntsc, c1_ball_switch_mes_aug6,  \
                        c1_ball_switch_mes_jun26)

static ActMail c2_ball_main_mes_pal[2] = {{406, actSt04rC2BallSwitch}, {429}}; /* derived name */

static ActMail c2_ball_main_mes_ntsc[2] = {{376, actSt04rC2BallSwitch}, {396}}; /* derived name */

static ActMail c2_ball_main_mes_aug6[2] = {{372, actSt04rC2BallSwitch}, {392}}; /* derived name */

static ActMail c2_ball_main_mes_jun26[2] = {{356, actSt04rC2BallSwitch}, {376}}; /* derived name */

#define c2_ball_main_mes                                                                           \
    PAL_NTSC_AUG6_JUN26(c2_ball_main_mes_pal, c2_ball_main_mes_ntsc, c2_ball_main_mes_aug6,        \
                        c2_ball_main_mes_jun26)

static ActMail c2_ball_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c2_ball_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c2_ball_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail c2_ball_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define c2_ball_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(c2_ball_mes_pal, c2_ball_mes_ntsc, c2_ball_mes_aug6, c2_ball_mes_jun26)

static ActMail c2_ball_switch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c2_ball_switch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c2_ball_switch_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail c2_ball_switch_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define c2_ball_switch_mes                                                                         \
    PAL_NTSC_AUG6_JUN26(c2_ball_switch_mes_pal, c2_ball_switch_mes_ntsc, c2_ball_switch_mes_aug6,  \
                        c2_ball_switch_mes_jun26)

static ActMail c3_ball_main_mes_pal[2] = {{407, actSt04rC3BallSwitch}, {429}}; /* derived name */

static ActMail c3_ball_main_mes_ntsc[2] = {{377, actSt04rC3BallSwitch}, {396}}; /* derived name */

static ActMail c3_ball_main_mes_aug6[2] = {{373, actSt04rC3BallSwitch}, {392}}; /* derived name */

static ActMail c3_ball_main_mes_jun26[2] = {{357, actSt04rC3BallSwitch}, {376}}; /* derived name */

#define c3_ball_main_mes                                                                           \
    PAL_NTSC_AUG6_JUN26(c3_ball_main_mes_pal, c3_ball_main_mes_ntsc, c3_ball_main_mes_aug6,        \
                        c3_ball_main_mes_jun26)

static ActMail c3_ball_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c3_ball_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c3_ball_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail c3_ball_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define c3_ball_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(c3_ball_mes_pal, c3_ball_mes_ntsc, c3_ball_mes_aug6, c3_ball_mes_jun26)

static ActMail c3_ball_switch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c3_ball_switch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c3_ball_switch_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail c3_ball_switch_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define c3_ball_switch_mes                                                                         \
    PAL_NTSC_AUG6_JUN26(c3_ball_switch_mes_pal, c3_ball_switch_mes_ntsc, c3_ball_switch_mes_aug6,  \
                        c3_ball_switch_mes_jun26)

static ActMail crest01_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail crest01_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail crest01_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail crest01_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define crest01_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(crest01_mes_pal, crest01_mes_ntsc, crest01_mes_aug6, crest01_mes_jun26)

static ActMail crest02_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail crest02_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail crest02_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail crest02_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define crest02_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(crest02_mes_pal, crest02_mes_ntsc, crest02_mes_aug6, crest02_mes_jun26)

static ActMail crest03_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail crest03_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail crest03_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail crest03_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define crest03_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(crest03_mes_pal, crest03_mes_ntsc, crest03_mes_aug6, crest03_mes_jun26)

static ActMail solar_beam_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail solar_beam_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail solar_beam_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail solar_beam_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define solar_beam_mes                                                                             \
    PAL_NTSC_AUG6_JUN26(solar_beam_mes_pal, solar_beam_mes_ntsc, solar_beam_mes_aug6,              \
                        solar_beam_mes_jun26)

static ActMail solar_stage_change_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail solar_stage_change_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail solar_stage_change_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail solar_stage_change_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define solar_stage_change_mes                                                                     \
    PAL_NTSC_AUG6_JUN26(solar_stage_change_mes_pal, solar_stage_change_mes_ntsc,                   \
                        solar_stage_change_mes_aug6, solar_stage_change_mes_jun26)

static ActMail stair_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail stair_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail stair_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail stair_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define stair_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(stair_mes_pal, stair_mes_ntsc, stair_mes_aug6, stair_mes_jun26)

static ActMail sekizo_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sekizo_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail sekizo_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define sekizo_mes                                                                                 \
    PAL_NTSC_AUG6_JUN26(sekizo_mes_pal, sekizo_mes_ntsc, sekizo_mes_aug6, sekizo_mes_jun26)

static ActMail rope1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail rope1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail rope1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail rope1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define rope1_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(rope1_mes_pal, rope1_mes_ntsc, rope1_mes_aug6, rope1_mes_jun26)

static ActMail rope2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail rope2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail rope2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail rope2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define rope2_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(rope2_mes_pal, rope2_mes_ntsc, rope2_mes_aug6, rope2_mes_jun26)

static ActMail brg1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail brg1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail brg1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail brg1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define brg1_mes PAL_NTSC_AUG6_JUN26(brg1_mes_pal, brg1_mes_ntsc, brg1_mes_aug6, brg1_mes_jun26)

static ActMail brg1_chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope3_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail rope3_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail rope3_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail rope3_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define rope3_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(rope3_mes_pal, rope3_mes_ntsc, rope3_mes_aug6, rope3_mes_jun26)

static ActMail rope4_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail rope4_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail rope4_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail rope4_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define rope4_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(rope4_mes_pal, rope4_mes_ntsc, rope4_mes_aug6, rope4_mes_jun26)

static ActMail brg2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail brg2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail brg2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail brg2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define brg2_mes PAL_NTSC_AUG6_JUN26(brg2_mes_pal, brg2_mes_ntsc, brg2_mes_aug6, brg2_mes_jun26)

static ActMail brg2_chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg1_way_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail brg1_way_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail brg1_way_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail brg1_way_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define brg1_way_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(brg1_way_mes_pal, brg1_way_mes_ntsc, brg1_way_mes_aug6, brg1_way_mes_jun26)

static ActMail brg2_way_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail brg2_way_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail brg2_way_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail brg2_way_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define brg2_way_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(brg2_way_mes_pal, brg2_way_mes_ntsc, brg2_way_mes_aug6, brg2_way_mes_jun26)

static ActMail monyou01_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou01_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou01_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail monyou01_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define monyou01_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(monyou01_mes_pal, monyou01_mes_ntsc, monyou01_mes_aug6, monyou01_mes_jun26)

static ActMail monyou02_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou02_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou02_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail monyou02_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define monyou02_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(monyou02_mes_pal, monyou02_mes_ntsc, monyou02_mes_aug6, monyou02_mes_jun26)

static ActMail monyou03_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou03_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou03_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail monyou03_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define monyou03_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(monyou03_mes_pal, monyou03_mes_ntsc, monyou03_mes_aug6, monyou03_mes_jun26)

static ActMail monyou04_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou04_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou04_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail monyou04_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define monyou04_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(monyou04_mes_pal, monyou04_mes_ntsc, monyou04_mes_aug6, monyou04_mes_jun26)

static ActMail monyou05_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou05_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou05_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail monyou05_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define monyou05_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(monyou05_mes_pal, monyou05_mes_ntsc, monyou05_mes_aug6, monyou05_mes_jun26)

static ActMail monyou06_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou06_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou06_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail monyou06_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define monyou06_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(monyou06_mes_pal, monyou06_mes_ntsc, monyou06_mes_aug6, monyou06_mes_jun26)

static ActMail monyou07_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou07_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou07_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail monyou07_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define monyou07_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(monyou07_mes_pal, monyou07_mes_ntsc, monyou07_mes_aug6, monyou07_mes_jun26)

static ActMail barricade_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail barricade_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail barricade_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail barricade_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define barricade_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(barricade_mes_pal, barricade_mes_ntsc, barricade_mes_aug6,                 \
                        barricade_mes_jun26)

static ActMail gondola_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail gondola_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define gondola_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(gondola_mes_pal, gondola_mes_ntsc, gondola_mes_aug6, gondola_mes_jun26)

static ActMail gondola2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail gondola2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define gondola2_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(gondola2_mes_pal, gondola2_mes_ntsc, gondola2_mes_aug6, gondola2_mes_jun26)

static ActMail gondola_chk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_chk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_chk_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail gondola_chk_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define gondola_chk_mes                                                                            \
    PAL_NTSC_AUG6_JUN26(gondola_chk_mes_pal, gondola_chk_mes_ntsc, gondola_chk_mes_aug6,           \
                        gondola_chk_mes_jun26)

static ActMail gondola_chara_chk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_chara_chk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_chara_chk_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail gondola_chara_chk_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define gondola_chara_chk_mes                                                                      \
    PAL_NTSC_AUG6_JUN26(gondola_chara_chk_mes_pal, gondola_chara_chk_mes_ntsc,                     \
                        gondola_chara_chk_mes_aug6, gondola_chara_chk_mes_jun26)

static ActMail torch1_1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch1_1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch1_1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail torch1_1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define torch1_1_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(torch1_1_mes_pal, torch1_1_mes_ntsc, torch1_1_mes_aug6, torch1_1_mes_jun26)

static ActMail torch1_2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch1_2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch1_2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail torch1_2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define torch1_2_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(torch1_2_mes_pal, torch1_2_mes_ntsc, torch1_2_mes_aug6, torch1_2_mes_jun26)

static ActMail torch2_1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch2_1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail torch2_1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define torch2_1_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(torch2_1_mes_pal, torch2_1_mes_ntsc, torch2_1_mes_aug6, torch2_1_mes_jun26)

static ActMail torch2_2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch2_2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail torch2_2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define torch2_2_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(torch2_2_mes_pal, torch2_2_mes_ntsc, torch2_2_mes_aug6, torch2_2_mes_jun26)

static ActMail torch3_1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch3_1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch3_1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail torch3_1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define torch3_1_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(torch3_1_mes_pal, torch3_1_mes_ntsc, torch3_1_mes_aug6, torch3_1_mes_jun26)

static ActMail torch3_2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch3_2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch3_2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail torch3_2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define torch3_2_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(torch3_2_mes_pal, torch3_2_mes_ntsc, torch3_2_mes_aug6, torch3_2_mes_jun26)

static ActMail torch2_1xl_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_1xl_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch2_1xl_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail torch2_1xl_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define torch2_1xl_mes                                                                             \
    PAL_NTSC_AUG6_JUN26(torch2_1xl_mes_pal, torch2_1xl_mes_ntsc, torch2_1xl_mes_aug6,              \
                        torch2_1xl_mes_jun26)

static ActMail torch2_2xl_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_2xl_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch2_2xl_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail torch2_2xl_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define torch2_2xl_mes                                                                             \
    PAL_NTSC_AUG6_JUN26(torch2_2xl_mes_pal, torch2_2xl_mes_ntsc, torch2_2xl_mes_aug6,              \
                        torch2_2xl_mes_jun26)

/* .sbss: the pad actuator handle openGate holds, the demo's own end flag, the
   gate animation and voice clip openGate plays, and the flag the stair
   subthread raises once it is past its setup. */
static int padAct; /* derived name */

static int demoEnd;

static int gateAnim; /* derived name */

static int gateAdpcm; /* derived name */

static int subStarted; /* derived name */

/* The look-at point both characters face in the crest cutscene. */
static const ConstVec crestFacePos = {{0.0f, 0.0f, -5000.0f, 1.0f}}; /* derived name */

/* .sdata: the room's stream handles and shakes (the three balls and crest2_4r
   unused). */
SqEntry *solar4r = 0;

SqEntry *ball1_4r = 0;

SqEntry *ball2_4r = 0;

SqEntry *ball3_4r = 0;

SqEntry *crest1_4r = 0;

SqEntry *crest2_4r = 0;

SqEntry *crest3_4r = 0;

SqEntry *stair5d = 0;

SqEntry *sekizo5c = 0;

unsigned char st05d_hasi = 0;

/* the 2001 builds' stream handle under this name (the European program keeps a
   different value in it) */
SqEntry *st05d_hasi_ntscaug6jun26 = 0;

int st04r_yure = 0;

unsigned char st04r_yure_vol = 0;

int sekizo_4r = 0;

unsigned char sekizo_4r_vol = 0;

static void actSt05eEnd_jun26(void)
{
    if (gflagChk(200) == 0) {
        soundDataClose(solar4r);
    }
}

static void actSt05eEnd_ntscaug6(void)
{
    if (solar4r != 0) {
        if (gflagChk(209) == 0) {
            soundDataClose(solar4r);
        }
    }
}

void actSt05eEnd(void)
{
    if (ICO_JUN26) {
        actSt05eEnd_jun26();
        return;
    }
    if (ICO_NTSCAUG6) {
        actSt05eEnd_ntscaug6();
        return;
    }
    if (solar4r != 0) {
        if (gflagChk(234) == 0) {
            scpAdpcmCloseFunc(&solar4r);
        }
    }
}

static void actSt05cInit_ntsc(void)
{
    if (gflagChk(249) == 0) {
        stage_SetAnimation(304, 0, 0);
    } else {
        stage_SetAnimation(304, 0, -1);
    }

    if (gflagChk(250) == 0) {
        stage_SetAnimation(305, 0, 0);
    } else {
        stage_SetAnimation(305, 0, -1);
    }

    if (gflagChk(251) == 0) {
        stage_SetAnimation(306, 0, 0);
    } else {
        stage_SetAnimation(306, 0, -1);
    }

    if (gflagChk(252) == 0) {
        stage_SetAnimation(307, 0, 0);
    } else {
        stage_SetAnimation(307, 0, -1);
    }

    if (gflagChk(253) == 0) {
        stage_SetAnimation(308, 0, 0);
    } else {
        stage_SetAnimation(308, 0, -1);
    }

    if (gflagChk(254) == 0) {
        stage_SetAnimation(309, 0, 0);
    } else {
        stage_SetAnimation(309, 0, -1);
    }

    if (gflagChk(255) == 0) {
        stage_SetAnimation(310, 0, 0);
    } else {
        stage_SetAnimation(310, 0, -1);
    }

    if (gflagChk(229) == 0) {
        SetWayGroupActive(1, 0);
    } else {
        SetWayGroupActive(1, 1);
    }

    if (gflagChk(234) == 0) {
        stage_SetAnimation(295, 0, 0);
    } else {
        stage_SetAnimation(295, 0, -1);
    }

    if (gflagChk(235) == 0) {
        stage_SetAnimation(296, 0, 0);
    } else {
        stage_SetAnimation(296, 0, -1);
    }

    if (gflagChk(228) == 0) {
        scpSearchGobj(1279)->active = 0;
    }
}

static void actSt05cInit_jun26(void)
{
    if (gflagChk(240) == 0) {
        stage_SetAnimation(271, 0, 0);
    } else {
        stage_SetAnimation(271, 0, -1);
    }

    if (gflagChk(241) == 0) {
        stage_SetAnimation(272, 0, 0);
    } else {
        stage_SetAnimation(272, 0, -1);
    }

    if (gflagChk(242) == 0) {
        stage_SetAnimation(273, 0, 0);
    } else {
        stage_SetAnimation(273, 0, -1);
    }

    if (gflagChk(243) == 0) {
        stage_SetAnimation(274, 0, 0);
    } else {
        stage_SetAnimation(274, 0, -1);
    }

    if (gflagChk(244) == 0) {
        stage_SetAnimation(275, 0, 0);
    } else {
        stage_SetAnimation(275, 0, -1);
    }

    if (gflagChk(245) == 0) {
        stage_SetAnimation(276, 0, 0);
    } else {
        stage_SetAnimation(276, 0, -1);
    }

    if (gflagChk(246) == 0) {
        SetWayGroupActive(1, 0);
        stage_SetAnimation(277, 0, 0);
    } else {
        SetWayGroupActive(1, 1);
        stage_SetAnimation(277, 0, -1);
    }

    if (gflagChk(225) == 0) {
        stage_SetAnimation(264, 0, 0);
    } else {
        stage_SetAnimation(264, 0, -1);
    }

    if (gflagChk(226) == 0) {
        stage_SetAnimation(265, 0, 0);
    } else {
        stage_SetAnimation(265, 0, -1);
    }
}

static void actSt05cInit_aug6(void)
{
    if (gflagChk(249) == 0) {
        stage_SetAnimation(303, 0, 0);
    } else {
        stage_SetAnimation(303, 0, -1);
    }

    if (gflagChk(250) == 0) {
        stage_SetAnimation(304, 0, 0);
    } else {
        stage_SetAnimation(304, 0, -1);
    }

    if (gflagChk(251) == 0) {
        stage_SetAnimation(305, 0, 0);
    } else {
        stage_SetAnimation(305, 0, -1);
    }

    if (gflagChk(252) == 0) {
        stage_SetAnimation(306, 0, 0);
    } else {
        stage_SetAnimation(306, 0, -1);
    }

    if (gflagChk(253) == 0) {
        stage_SetAnimation(307, 0, 0);
    } else {
        stage_SetAnimation(307, 0, -1);
    }

    if (gflagChk(254) == 0) {
        stage_SetAnimation(308, 0, 0);
    } else {
        stage_SetAnimation(308, 0, -1);
    }

    if (gflagChk(255) == 0) {
        stage_SetAnimation(309, 0, 0);
    } else {
        stage_SetAnimation(309, 0, -1);
    }

    if (gflagChk(229) == 0) {
        SetWayGroupActive(1, 0);
    } else {
        SetWayGroupActive(1, 1);
    }

    if (gflagChk(234) == 0) {
        stage_SetAnimation(294, 0, 0);
    } else {
        stage_SetAnimation(294, 0, -1);
    }

    if (gflagChk(235) == 0) {
        stage_SetAnimation(295, 0, 0);
    } else {
        stage_SetAnimation(295, 0, -1);
    }

    if (gflagChk(228) == 0) {
        scpSearchGobj(1275)->active = 0;
    }
}

void actSt05cInit(void)
{
    if (ICO_JUN26) {
        actSt05cInit_jun26();
        return;
    }
    if (ICO_NTSC) {
        actSt05cInit_ntsc();
        return;
    }
    if (ICO_AUG6) {
        actSt05cInit_aug6();
        return;
    }
    if (gflagChk(274) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(337, 333), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(337, 333), 0, -1);
    }

    if (gflagChk(275) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(338, 334), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(338, 334), 0, -1);
    }

    if (gflagChk(276) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(339, 335), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(339, 335), 0, -1);
    }

    if (gflagChk(277) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(340, 336), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(340, 336), 0, -1);
    }

    if (gflagChk(278) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(341, 337), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(341, 337), 0, -1);
    }

    if (gflagChk(279) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(342, 338), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(342, 338), 0, -1);
    }

    if (gflagChk(280) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(343, 339), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(343, 339), 0, -1);
    }

    if (gflagChk(254) == 0) {
        SetWayGroupActive(1, 0);
    } else {
        SetWayGroupActive(1, 1);
    }

    if (gflagChk(259) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(328, 324), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(328, 324), 0, -1);
    }

    if (gflagChk(260) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(329, 325), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(329, 325), 0, -1);
    }

    if (gflagChk(253) == 0) {
        scpSearchGobj(1379)->active = 0;
    }
}

static void actSt05dInit_ntsc(void)
{
    if (gflagChk(234) == 0) {
        stage_SetAnimation(295, 0, 0);

        SetWayGroupActive(3, 0);
    } else {
        stage_SetAnimation(295, 0, -1);

        SetWayGroupActive(3, 1);
    }

    if (gflagChk(235) == 0) {
        stage_SetAnimation(296, 0, 0);

        SetWayGroupActive(4, 0);
    } else {
        stage_SetAnimation(296, 0, -1);

        SetWayGroupActive(4, 1);
    }

    if (gflagChk(228) == 0) {
        stage_SetAnimation(297, 0, 0);
        stage_SetAnimation(317, 0, 0);
    } else {
        stage_SetAnimation(297, 0, -1);
        stage_SetAnimation(317, 0, -1);
    }

    if (gflagChk(249) == 0) {
        stage_SetAnimation(304, 0, 0);
    } else {
        stage_SetAnimation(304, 0, -1);
    }

    if (gflagChk(250) == 0) {
        stage_SetAnimation(305, 0, 0);
    } else {
        stage_SetAnimation(305, 0, -1);
    }

    if (gflagChk(251) == 0) {
        stage_SetAnimation(306, 0, 0);
    } else {
        stage_SetAnimation(306, 0, -1);
    }

    if (gflagChk(252) == 0) {
        stage_SetAnimation(307, 0, 0);
    } else {
        stage_SetAnimation(307, 0, -1);
    }

    if (gflagChk(253) == 0) {
        stage_SetAnimation(308, 0, 0);
    } else {
        stage_SetAnimation(308, 0, -1);
    }

    if (gflagChk(254) == 0) {
        stage_SetAnimation(309, 0, 0);
    } else {
        stage_SetAnimation(309, 0, -1);
    }

    if (gflagChk(255) == 0) {
        stage_SetAnimation(310, 0, 0);
    } else {
        stage_SetAnimation(310, 0, -1);
    }

    if (gflagChk(148) != 0 && gflagChk(209) == 0) {
        stage_SetAnimation(314, 0, -1);
    }

    if (gflagChk(148) == 0 || gflagChk(209) != 0) {
        stage_SetAnimation(314, 0, 0);
    }

    if (gflagChk(257) != 0) {
        stage_SetAnimation(201, 0, 200);
    } else {
        stage_SetAnimation(201, 0, 0);
    }

    if (gflagChk(207) == 0) {
        scpSearchGobj(1353)->active = 0;
    } else {
        scpSearchGobj(1352)->active = 0;
    }
}

static void actSt05dInit_jun26(void)
{
    if (gflagChk(225) == 0) {
        stage_SetAnimation(264, 0, 0);

        SetWayGroupActive(3, 0);
    } else {
        stage_SetAnimation(264, 0, -1);

        SetWayGroupActive(3, 1);
    }

    if (gflagChk(226) == 0) {
        stage_SetAnimation(265, 0, 0);

        SetWayGroupActive(4, 0);
    } else {
        stage_SetAnimation(265, 0, -1);

        SetWayGroupActive(4, 1);
    }

    if (gflagChk(219) == 0) {
        stage_SetAnimation(266, 0, 0);
        stage_SetAnimation(284, 0, 0);
    } else {
        stage_SetAnimation(266, 0, -1);
        stage_SetAnimation(284, 0, -1);
    }

    if (gflagChk(240) == 0) {
        stage_SetAnimation(271, 0, 0);
    } else {
        stage_SetAnimation(271, 0, -1);
    }

    if (gflagChk(241) == 0) {
        stage_SetAnimation(272, 0, 0);
    } else {
        stage_SetAnimation(272, 0, -1);
    }

    if (gflagChk(242) == 0) {
        stage_SetAnimation(273, 0, 0);
    } else {
        stage_SetAnimation(273, 0, -1);
    }

    if (gflagChk(243) == 0) {
        stage_SetAnimation(274, 0, 0);
    } else {
        stage_SetAnimation(274, 0, -1);
    }

    if (gflagChk(244) == 0) {
        stage_SetAnimation(275, 0, 0);
    } else {
        stage_SetAnimation(275, 0, -1);
    }

    if (gflagChk(245) == 0) {
        stage_SetAnimation(276, 0, 0);
    } else {
        stage_SetAnimation(276, 0, -1);
    }

    if (gflagChk(246) == 0) {
        stage_SetAnimation(277, 0, 0);
    } else {
        stage_SetAnimation(277, 0, -1);
    }

    if (gflagChk(141) != 0 && gflagChk(200) == 0) {
        stage_SetAnimation(281, 0, -1);
    }

    if (gflagChk(141) == 0 || gflagChk(200) != 0) {
        stage_SetAnimation(281, 0, 0);
    }

    if (gflagChk(248) != 0) {
        stage_SetAnimation(180, 0, 200);
    } else {
        stage_SetAnimation(180, 0, 0);
    }

    if (gflagChk(198) == 0) {
        scpSearchGobj(1287)->active = 0;
    } else {
        scpSearchGobj(1286)->active = 0;
    }
}

static void actSt05dInit_aug6(void)
{
    if (gflagChk(234) == 0) {
        stage_SetAnimation(294, 0, 0);

        SetWayGroupActive(3, 0);
    } else {
        stage_SetAnimation(294, 0, -1);

        SetWayGroupActive(3, 1);
    }

    if (gflagChk(235) == 0) {
        stage_SetAnimation(295, 0, 0);

        SetWayGroupActive(4, 0);
    } else {
        stage_SetAnimation(295, 0, -1);

        SetWayGroupActive(4, 1);
    }

    if (gflagChk(228) == 0) {
        stage_SetAnimation(296, 0, 0);
        stage_SetAnimation(316, 0, 0);
    } else {
        stage_SetAnimation(296, 0, -1);
        stage_SetAnimation(316, 0, -1);
    }

    if (gflagChk(249) == 0) {
        stage_SetAnimation(303, 0, 0);
    } else {
        stage_SetAnimation(303, 0, -1);
    }

    if (gflagChk(250) == 0) {
        stage_SetAnimation(304, 0, 0);
    } else {
        stage_SetAnimation(304, 0, -1);
    }

    if (gflagChk(251) == 0) {
        stage_SetAnimation(305, 0, 0);
    } else {
        stage_SetAnimation(305, 0, -1);
    }

    if (gflagChk(252) == 0) {
        stage_SetAnimation(306, 0, 0);
    } else {
        stage_SetAnimation(306, 0, -1);
    }

    if (gflagChk(253) == 0) {
        stage_SetAnimation(307, 0, 0);
    } else {
        stage_SetAnimation(307, 0, -1);
    }

    if (gflagChk(254) == 0) {
        stage_SetAnimation(308, 0, 0);
    } else {
        stage_SetAnimation(308, 0, -1);
    }

    if (gflagChk(255) == 0) {
        stage_SetAnimation(309, 0, 0);
    } else {
        stage_SetAnimation(309, 0, -1);
    }

    if (gflagChk(148) != 0 && gflagChk(209) == 0) {
        stage_SetAnimation(313, 0, -1);
    }

    if (gflagChk(148) == 0 || gflagChk(209) != 0) {
        stage_SetAnimation(313, 0, 0);
    }

    if (gflagChk(257) != 0) {
        stage_SetAnimation(200, 0, 200);
    } else {
        stage_SetAnimation(200, 0, 0);
    }

    if (gflagChk(207) == 0) {
        scpSearchGobj(1349)->active = 0;
    } else {
        scpSearchGobj(1348)->active = 0;
    }
}

void actSt05dInit(void)
{
    if (ICO_JUN26) {
        actSt05dInit_jun26();
        return;
    }
    if (ICO_NTSC) {
        actSt05dInit_ntsc();
        return;
    }
    if (ICO_AUG6) {
        actSt05dInit_aug6();
        return;
    }
    if (gflagChk(259) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(328, 324), 0, 0);

        SetWayGroupActive(3, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(328, 324), 0, -1);

        SetWayGroupActive(3, 1);
    }

    if (gflagChk(260) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(329, 325), 0, 0);

        SetWayGroupActive(4, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(329, 325), 0, -1);

        SetWayGroupActive(4, 1);
    }

    if (gflagChk(253) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(330, 326), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN(350, 346), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(330, 326), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(350, 346), 0, -1);

        FinishHint(24);
    }

    if (gflagChk(274) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(337, 333), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(337, 333), 0, -1);
    }

    if (gflagChk(275) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(338, 334), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(338, 334), 0, -1);
    }

    if (gflagChk(276) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(339, 335), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(339, 335), 0, -1);
    }

    if (gflagChk(277) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(340, 336), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(340, 336), 0, -1);
    }

    if (gflagChk(278) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(341, 337), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(341, 337), 0, -1);
    }

    if (gflagChk(279) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(342, 338), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(342, 338), 0, -1);
    }

    if (gflagChk(280) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(343, 339), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(343, 339), 0, -1);
    }

    if (gflagChk(165) != 0 && gflagChk(234) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(347, 343), 0, -1);
    }

    if (gflagChk(165) == 0 || gflagChk(234) != 0) {
        stage_SetAnimation(PAL_JPNKORCN(347, 343), 0, 0);
    }

    if (gflagChk(282) != 0) {
        stage_SetAnimation(PAL_JPNKORCN(229, 225), 0, 200);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(229, 225), 0, 0);
    }
}

void actSt05eInit(void)
{
    if (gflagChk(PAL_NTSCAUG6_JUN26(231, 206, 197)) == 0) {
        SetWayGroupActive(5, 0);
    } else {
        SetWayGroupActive(5, 1);
    }

    if (gflagChk(PAL_NTSCAUG6_JUN26(259, 234, 225)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(328, 324, 295, 294, 264), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(328, 324, 295, 294, 264), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6_JUN26(260, 235, 226)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(329, 325, 296, 295, 265), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(329, 325, 296, 295, 265), 0, -1);
    }
}

static void actSt04rCrest02_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(210) == 0) {
        if (current_stage_no == 28) {
            stage_SetAnimation(251, 0, 0);
        } else {
            stage_SetAnimation(252, 0, 0);
        }

        crest02_mes[0].func = actSt04rCrest2Main;
        act->mail = crest02_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        if (current_stage_no == 28) {
            stage_SetAnimation(251, 0, -1);
        } else {
            stage_SetAnimation(252, 0, -1);
        }
    }
}

void actSt04rCrest02(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rCrest02_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(244, 219)) == 0) {
        if (current_stage_no == 28) {
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(311, 307, 278, 277), 0, 0);
        } else {
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(312, 308, 279, 278), 0, 0);
        }

        crest02_mes[0].func = actSt04rCrest2Main;
        act->mail = crest02_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (current_stage_no == 28) {
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(311, 307, 278, 277), 0, -1);
        } else {
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(312, 308, 279, 278), 0, -1);
        }

        scpBoyControlReadDisable = 0;
    }
}

static void openGateSub(GObj *volatile self)
{
    stage_SetAnimation(gateAnim, 1, 0);

    while (stage_CheckAnimationFrame(gateAnim, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    padAct = iosPadActRequest(boyPad, 10);
    st05d_hasi = 128;
    iosPadActVolumeSet(padAct, 128);

    while (stage_CheckAnimationFrame(gateAnim, 190, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(padAct);

    while (stage_CheckAnimationFinish(gateAnim) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

static void openGate(GObj *self)
{
    GProc *th;

    lt_switch_layout(PAL_JPNKORCN(55, 52));

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(gateAdpcm, &crest1_4r, 0, 1, 1);

    while (crest1_4r == 0) {
        _ACTWait(1);
    }

    th = actCreateSubThread(openGateSub, 21);

    demoEnd = 0;
    padAct = -1;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&((GProc *)th)->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&crest1_4r, 256);

        if (padAct >= 0) {
            iosPadActStop(padAct);
        }

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(gateAnim, 0, -1);

        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();

    lt_switch_layout(PAL_JPNKORCN(54, 51));

    scpBoyControlReadDisable = 0;
}

static void actSt04rCrest2Main_ntsc(GObj *volatile self)
{
    if (current_stage_no == 28) {
        while (scpIsTorchLightOn(1308) == 0 || scpIsTorchLightOn(1309) == 0 || gflagChk(226) == 0) {
            _ACTWait(1);
        }
    }

    if (current_stage_no == 29) {
        while (scpIsTorchLightOn(1382) == 0 || scpIsTorchLightOn(1383) == 0 || gflagChk(226) == 0) {
            _ACTWait(1);
        }
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(24, &crest2_4r, 0, 1, 1);

    while (crest2_4r == 0) {
        _ACTWait(1);
    }

    if (current_stage_no == 28) {
        stage_SetAnimation(278, 1, 0);

        while (stage_CheckAnimationFrame(278, 30, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        st04r_yure = iosPadActRequest(boyPad, 10);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);

        while (stage_CheckAnimationFrame(278, 190, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        iosPadActStop(st04r_yure);

        while (stage_CheckAnimationFinish(278) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOn(223);
    } else {
        stage_SetAnimation(279, 1, 0);

        while (stage_CheckAnimationFrame(279, 30, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        st04r_yure = iosPadActRequest(boyPad, 10);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);

        while (stage_CheckAnimationFrame(279, 190, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        iosPadActStop(st04r_yure);

        while (stage_CheckAnimationFinish(279) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOn(222);
    }

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
    _ACTWait(1);

    gflagOn(219);
    gflagOff(221);
    gflagOff(224);
    gflagOff(208);
}

static void actSt04rCrest2Main_jun26(GObj *volatile self)
{
    if (current_stage_no == PAL_JUN26(28, 0)) {
        while (scpIsTorchLightOn(1243) == 0 || scpIsTorchLightOn(1244) == 0 || gflagChk(217) == 0) {
            _ACTWait(1);
        }
    }

    if (current_stage_no == 29) {
        while (scpIsTorchLightOn(1315) == 0 || scpIsTorchLightOn(1316) == 0 || gflagChk(217) == 0) {
            _ACTWait(1);
        }
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(27, &crest2_4r, 0, 1, 1);

    while (crest2_4r == 0) {
        _ACTWait(1);
    }

    if (current_stage_no == 28) {
        stage_SetAnimation(251, 1, 0);

        while (stage_CheckAnimationFrame(251, 30, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        st04r_yure = iosPadActRequest(boyPad, 10);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);

        while (stage_CheckAnimationFrame(251, 190, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        iosPadActStop(st04r_yure);

        while (stage_CheckAnimationFinish(251) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOn(214);
    } else {
        stage_SetAnimation(252, 1, 0);

        while (stage_CheckAnimationFrame(252, 30, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        st04r_yure = iosPadActRequest(boyPad, 10);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);

        while (stage_CheckAnimationFrame(252, 190, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        iosPadActStop(st04r_yure);

        while (stage_CheckAnimationFinish(252) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOn(213);
    }

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
    _ACTWait(1);

    gflagOn(210);
    gflagOff(212);
    gflagOff(215);
    gflagOff(199);
}

static void actSt04rCrest2Main_aug6(GObj *volatile self)
{
    if (current_stage_no == 28) {
        while (scpIsTorchLightOn(1304) == 0 || scpIsTorchLightOn(1305) == 0 || gflagChk(226) == 0) {
            _ACTWait(1);
        }
    }

    if (current_stage_no == 29) {
        while (scpIsTorchLightOn(1376) == 0 || scpIsTorchLightOn(1377) == 0 || gflagChk(226) == 0) {
            _ACTWait(1);
        }
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(24, &crest2_4r, 0, 1, 1);

    while (crest2_4r == 0) {
        _ACTWait(1);
    }

    if (current_stage_no == 28) {
        stage_SetAnimation(277, 1, 0);

        while (stage_CheckAnimationFrame(277, 30, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        st04r_yure = iosPadActRequest(boyPad, 10);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);

        while (stage_CheckAnimationFrame(277, 190, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        iosPadActStop(st04r_yure);

        while (stage_CheckAnimationFinish(277) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOn(223);
    } else {
        stage_SetAnimation(278, 1, 0);

        while (stage_CheckAnimationFrame(278, 30, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        st04r_yure = iosPadActRequest(boyPad, 10);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);

        while (stage_CheckAnimationFrame(278, 190, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        iosPadActStop(st04r_yure);

        while (stage_CheckAnimationFinish(278) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOn(222);
    }

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
    _ACTWait(1);

    gflagOn(219);
    gflagOff(221);
    gflagOff(224);
    gflagOff(208);
}

void actSt04rCrest2Main(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rCrest2Main_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt04rCrest2Main_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04rCrest2Main_aug6(self);
        return;
    }
    int anim;

    if (current_stage_no == 28) {
        anim = PAL_JPNKORCN(311, 307);
        while (scpIsTorchLightOn(1408) == 0 || scpIsTorchLightOn(1409) == 0 || gflagChk(251) == 0) {
            _ACTWait(1);
        }

        gateAnim = anim;
    }

    if (current_stage_no == 29) {
        anim = PAL_JPNKORCN(312, 308);

        while (scpIsTorchLightOn(1487) == 0 || scpIsTorchLightOn(1488) == 0 || gflagChk(251) == 0) {
            _ACTWait(1);
        }

        gateAnim = anim;
    }

    gateAdpcm = 25;
    openGate(self);

    if (current_stage_no == 28) {
        gflagOn(248);
    } else {
        gflagOn(247);
    }

    _ACTWait(1);

    gflagOn(244);
    gflagOff(246);
    gflagOff(249);
    gflagOff(233);
}

static void actSt04rCrest3Main_jun26(GObj *volatile self)
{
    while (scpIsTorchLightOn(1317) == 0 || scpIsTorchLightOn(1318) == 0 || gflagChk(218) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;

    gflagOn(145);

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(26, &crest3_4r, 0, 1, 1);

    while (crest3_4r == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(253, 1, 0);

    while (stage_CheckAnimationFrame(253, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    st04r_yure = iosPadActRequest(boyPad, 10);
    st04r_yure_vol = 128;
    iosPadActVolumeSet(st04r_yure, 128);

    while (stage_CheckAnimationFrame(253, 190, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st04r_yure);

    while (stage_CheckAnimationFinish(253) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;

    fightSoundProcessRequestStart();
    _ACTWait(1);

    gflagOn(211);
    gflagOff(212);
    gflagOff(213);
    gflagOn(215);
    gflagOff(199);
}

void actSt04rCrest3Main(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rCrest3Main_jun26(self);
        return;
    }
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1489, 1384, 1378)) == 0 ||
           scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1490, 1385, 1379)) == 0 ||
           gflagChk(PAL_NTSCAUG6JUN26(252, 227)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        lt_switch_layout(51);
    } else {
        scpSleepEnemyOne(PAL_JPNKORCN(3757, 3734));
    }

    if (ICO_NTSC) {
        scpBoyControlReadDisable = 1;
        scpSleepEnemyOne(3453);

        gflagOn(152);
    } else if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 1;
        scpSleepEnemyOne(3415);

        gflagOn(152);
    } else {
        gflagOn(170);
    }

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        scpAdpcmPlayRequestFunc(23, &crest3_4r, 0, 1, 1);
    } else {
        gateAnim = PAL_JPNKORCN(313, 309);
        gateAdpcm = 24;
        openGate(self);
    }

    if (ICO_NTSC) {
        while (crest3_4r == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(280, 1, 0);

        while (stage_CheckAnimationFrame(280, 30, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        st04r_yure = iosPadActRequest(boyPad, 10);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);

        while (stage_CheckAnimationFrame(280, 190, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        iosPadActStop(st04r_yure);

        while (stage_CheckAnimationFinish(280) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpWakeupEnemyOne(3453);

        scpBoyControlReadDisable = 0;

        lt_switch_layout(50);
    } else if (ICO_AUG6JUN26) {
        while (crest3_4r == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(279, 1, 0);

        while (stage_CheckAnimationFrame(279, 30, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        st04r_yure = iosPadActRequest(boyPad, 10);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);

        while (stage_CheckAnimationFrame(279, 190, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        iosPadActStop(st04r_yure);

        while (stage_CheckAnimationFinish(279) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpWakeupEnemyOne(3415);

        scpBoyControlReadDisable = 0;

        lt_switch_layout(50);
    } else {
        scpWakeupEnemyOne(PAL_JPNKORCN(3757, 3734));
    }

    fightSoundProcessRequestStart();
    _ACTWait(1);

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(220);
        gflagOff(221);
        gflagOff(222);
        gflagOn(224);
        gflagOff(208);
    } else {
        gflagOn(245);
        gflagOff(246);
        gflagOff(247);
        gflagOff(248);
        gflagOn(249);
        gflagOff(233);
    }
}

static void actSt04rSolarBeam_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    solar4r = 0;

    if (gflagChk(200) == 0) {
        scpAdpcmPlayRequestFunc(30, &solar4r, 0, 1, 0);

        if (gflagChk(209) != 0 && gflagChk(210) != 0 && gflagChk(211) != 0 && gflagChk(198) != 0) {
            scpFadeOut(255.0f, 0, 0, 0);

            while (solar4r == 0) {
                _ACTWait(1);
            }

            scpFadeIn(6.0f);
        }

        solar_beam_mes[0].func = actSt04rSolarBeamChk;
        act->mail = solar_beam_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    }
}

void actSt04rSolarBeam(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rSolarBeam_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    solar4r = 0;

    if (gflagChk(PAL_NTSCAUG6JUN26(234, 209)) == 0) {
        scpAdpcmPlayRequestFunc(PAL_NTSCAUG6JUN26(28, 27), &solar4r, 0, 1, 0);

        if (gflagChk(PAL_NTSCAUG6JUN26(243, 218)) != 0 &&
            gflagChk(PAL_NTSCAUG6JUN26(244, 219)) != 0 &&
            gflagChk(PAL_NTSCAUG6JUN26(245, 220)) != 0 &&
            gflagChk(PAL_NTSCAUG6JUN26(232, 207)) != 0) {
            if (boyGObj != 0) {
                scpPlayMot(boyGObj, 0);
            }

            if (!ICO_NTSCAUG6JUN26) {
                lt_switch_layout(PAL_JPNKORCN(55, 52));
            }

            scpBoyControlReadDisable = 1;
            gflagOn(PAL_NTSC_AUG6JUN26(389, 356, 355));

            scpFadeOut(255.0f, 0, 0, 0);

            while (solar4r == 0) {
                _ACTWait(1);
            }

            scpFadeIn(6.0f);
        }

        solar_beam_mes[0].func = actSt04rSolarBeamChk;
        act->mail = solar_beam_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        scpBoyControlReadDisable = 0;
    }
}

static void actSt04rSolarBeamChk_ntsc(GObj *volatile self)
{
    while (gflagChk(218) == 0 || gflagChk(219) == 0 || gflagChk(220) == 0 || gflagChk(207) == 0) {
        _ACTWait(1);
    }

    gflagOn(209);

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    while (solar4r == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(solar4r->stream);

    preload(15);

    stage_SetAnimation(313, 1, 0);
    stage_SetAnimation(261, 1, 0);
    _ACTWait(30);

    while (stage_CheckAnimationFinish(261) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;

    scpWakeupEnemyAll();

    RequestStageChange(15, boyGObj, girlGObj, 2.0f, 8.0f);
}

static void actSt04rSolarBeamChk_jun26(GObj *volatile self)
{
    while (gflagChk(209) == 0 || gflagChk(210) == 0 || gflagChk(211) == 0 || gflagChk(198) == 0) {
        _ACTWait(1);
    }

    gflagOn(200);

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    while (solar4r == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(solar4r->stream);

    stage_SetAnimation(280, 1, 0);
    stage_SetAnimation(234, 1, 0);
    _ACTWait(30);

    while (stage_CheckAnimationFinish(234) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;

    RequestStageChange(15, boyGObj, girlGObj, 16.0f, 16.0f);
}

static void actSt04rSolarBeamChk_aug6(GObj *volatile self)
{
    while (gflagChk(218) == 0 || gflagChk(219) == 0 || gflagChk(220) == 0 || gflagChk(207) == 0) {
        _ACTWait(1);
    }

    gflagOn(209);

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    while (solar4r == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(solar4r->stream);

    preload(15);

    stage_SetAnimation(312, 1, 0);
    stage_SetAnimation(260, 1, 0);
    _ACTWait(30);

    while (stage_CheckAnimationFinish(260) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;

    scpWakeupEnemyAll();

    RequestStageChange(15, boyGObj, girlGObj, 2.0f, 8.0f);
}

void actSt04rSolarBeamChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rSolarBeamChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt04rSolarBeamChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04rSolarBeamChk_aug6(self);
        return;
    }
    while (gflagChk(243) == 0 || gflagChk(244) == 0 || gflagChk(245) == 0 || gflagChk(232) == 0) {
        _ACTWait(1);
    }

    gflagOn(234);

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    while (solar4r == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(solar4r->stream);

    preload(15);

    stage_SetAnimation(PAL_JPNKORCN(346, 342), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(294, 290), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(294, 290)) == 0) {
        if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            scpAdpcmFadeCloseFunc(&solar4r, 256);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKORCN(346, 342), 0, -1);
            stage_SetAnimation(PAL_JPNKORCN(294, 290), 0, -1);

            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 0;

    scpWakeupEnemyAll();

    RequestStageChange(15, boyGObj, 0, 2.0f, 8.0f);
}

static void actSt04rStairSub(GObj *volatile self)
{
    long long buf[2];
    float dir[4];

    _ACTWait(60);

    while (crest3_4r == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(crest3_4r->stream);

    if (ICO_JPNKORCN) {
        stage_SetAnimation(346, 1, 0);
    } else {
        stage_SetAnimation(350, 1, 0);

        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
    }

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);

    scpPlayPosSet(boyGObj, -55.0f, 28.0f, -3881.0f);
    scpPlayPosSet(girlGObj, 58.0f, 28.0f, -3891.0f);

    buf[0] = crestFacePos.d[0];
    buf[1] = crestFacePos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    buf[0] = crestFacePos.d[0];
    buf[1] = crestFacePos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    subStarted = 1;

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(350, 346)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    stage_SetAnimation(PAL_JPNKORCN(330, 326), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(330, 326), 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    padAct = iosPadActRequest(boyPad, 9);
    st05d_hasi = 128;
    iosPadActVolumeSet(padAct, 128);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(330, 326)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04rStairChk_ntsc(GObj *volatile self)
{
    long long buf[2];
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0xB000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0xB000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    gflagOn(228);

    scpSleepEnemyAll();

    scpSearchGobj(1420)->active = 0;

    stage_SetAnimation(318, -1, -2);
    _ACTWait(60);

    scpAdpcmPlayRequestFunc(59, &stair5d, 1, 1, 1);

    while (stair5d == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(317, 1, 0);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);

    scpPlayPosSet(boyGObj, -55.0f, 28.0f, -3881.0f);
    scpPlayPosSet(girlGObj, 58.0f, 28.0f, -3891.0f);

    buf[0] = crestFacePos.d[0];
    buf[1] = crestFacePos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    buf[0] = crestFacePos.d[0];
    buf[1] = crestFacePos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    while (stage_CheckAnimationFinish(317) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    stage_SetAnimation(297, 1, 0);

    while (stage_CheckAnimationFrame(297, 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    st04r_yure = iosPadActRequest(boyPad, 9);
    st04r_yure_vol = 128;
    iosPadActVolumeSet(st04r_yure, 128);

    while (stage_CheckAnimationFinish(297) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st04r_yure);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

static void actSt04rStairChk_aug6jun26(GObj *volatile self)
{
    long long buf[2];
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0xB000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0xB000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    gflagOn(PAL_JUN26(228, 219));

    scpSleepEnemyAll();

    scpSearchGobj(PAL_JUN26(1414, 1348))->active = 0;

    stage_SetAnimation(PAL_JUN26(317, 285), -1, -2);
    _ACTWait(60);

    if (ICO_JUN26) {
        scpAdpcmPlayRequestFunc(56, 0, 1, 1, 1);
    } else {
        scpAdpcmPlayRequestFunc(59, &stair5d, 1, 1, 1);
    }

    if (ICO_JUN26) {
        stage_SetAnimation(284, 1, 0);
    } else {
        while (stair5d == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(316, 1, 0);
    }

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, PAL_JUN26(514, 521));

    scpPlayPosSet(boyGObj, -55.0f, 28.0f, -3881.0f);
    scpPlayPosSet(girlGObj, 58.0f, 28.0f, -3891.0f);

    buf[0] = crestFacePos.d[0];
    buf[1] = crestFacePos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    buf[0] = crestFacePos.d[0];
    buf[1] = crestFacePos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    while (stage_CheckAnimationFinish(PAL_JUN26(316, 284)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    stage_SetAnimation(PAL_JUN26(296, 266), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JUN26(296, 266), 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    st04r_yure = iosPadActRequest(boyPad, 9);
    st04r_yure_vol = 128;
    iosPadActVolumeSet(st04r_yure, 128);

    while (stage_CheckAnimationFinish(PAL_JUN26(296, 266)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st04r_yure);

    scpPlayMot(boyGObj, 0);
    if (ICO_JUN26) {
        scpPlayMot(girlGObj, 521);
    } else {
        scpPlayMot(girlGObj, 514);

        scpWakeupEnemyAll();
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

void actSt04rStairChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rStairChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rStairChk_aug6jun26(self);
        return;
    }
    GProc *th;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0xB000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0xB000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));

    scpBoyControlReadDisable = 1;
    if (ICO_JPNKORCN) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
    }
    gflagOn(253);

    FinishHint(24);

    scpSleepEnemyAll();

    scpSearchGobj(1523)->active = 0;

    stage_SetAnimation(PAL_JPNKORCN(351, 347), -1, -2);

    scpAdpcmPlayRequestFunc(61, &crest3_4r, 1, 1, 0);

    th = actCreateSubThread(actSt04rStairSub, 21);

    demoEnd = 0;
    padAct = -1;
    subStarted = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (crest3_4r == 0) {
            _ACTWait(1);
        }

        scpAdpcmFadeCloseFunc(&crest3_4r, 512);

        while (subStarted == 0) {
            _ACTWait(1);
        }

        iosThreadSetPri(&((GProc *)th)->thread, 34);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPNKORCN(350, 346), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(330, 326), 0, -1);
        _ACTWait(1);

        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 532);

        scpPlayPosSet(boyGObj, -55.0f, 234.0f, -3881.0f);
        scpPlayPosSet(girlGObj, 58.0f, 234.0f, -3891.0f);

        _ACTWait(1);

        iosOmSendMail(girlGObj, 62, boyGObj);

        scpFadeIn(3.0f);
    } else {
        iosThreadSetPri(&((GProc *)th)->thread, 34);
    }

    if (padAct >= 0) {
        iosPadActStop(padAct);
    }

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt04rSekizoChk_jun26(GObj *volatile self)
{
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           scpTriggerFloorAttr(boyGObj, 0x8000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x8000000) == 0) {
        _ACTWait(1);
    }

    scpSekizouCheckPoint();

    scpAdpcmPlayRequestFunc(21, 0, 1, 1, 1);

    scpKillEnemyAll();

    scpMaskGeneratorAll();

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;

    gflagOn(220);
    _ACTWait(60);

    stage_SetAnimation(267, 1, 0);

    sekizo_4r = iosPadActRequest(boyPad, 9);
    sekizo_4r_vol = 128;
    iosPadActVolumeSet(sekizo_4r, 128);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 521);

    scpPlayPosSet(girlGObj, 0.0f, -1300.0f, -1700.0f);
    scpPlayPosSet(boyGObj, 20.0f, -1300.0f, -1700.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayMot(girlGObj, 584);
    scpPlayWaitMotEnd(girlGObj);

    while (stage_CheckAnimationFrame(267, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_4r);

    scpPlayMot(girlGObj, 521);
    scpPlayEnd(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
}

static void actSt04rSekizoChk_aug6(GObj *volatile self)
{
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           scpTriggerFloorAttr(boyGObj, 0x8000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x8000000) == 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(18, &sekizo5c, 1, 1, 1);

    while (sekizo5c == 0) {
        _ACTWait(1);
    }
    scpKillEnemyAll();

    scpMaskGeneratorAll();

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;

    SetWayGroupActive(1, 1);
    _ACTWait(60);

    stage_SetAnimation(297, 1, 0);

    sekizo_4r = iosPadActRequest(boyPad, 9);
    sekizo_4r_vol = 128;
    iosPadActVolumeSet(sekizo_4r, 128);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);

    scpPlayPosSet(girlGObj, 0.0f, -1300.0f, -1700.0f);
    scpPlayPosSet(boyGObj, 20.0f, -1300.0f, -1700.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, 573);
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(229);

    while (stage_CheckAnimationFrame(297, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_4r);

    scpPlayMot(girlGObj, 514);
    scpPlayEnd(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
}

static void actSt04rSekizoChk_ntsc(GObj *volatile self)
{
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           scpTriggerFloorAttr(boyGObj, 0x8000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x8000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpKillEnemyAll();

    scpMaskGeneratorAll();

    scpAdpcmPlayRequestFunc(18, &sekizo5c, 1, 1, 1);

    while (sekizo5c == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(1, 1);
    _ACTWait(60);

    stage_SetAnimation(298, 1, 0);

    sekizo_4r = iosPadActRequest(boyPad, 9);
    sekizo_4r_vol = 128;
    iosPadActVolumeSet(sekizo_4r, 128);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);

    scpPlayPosSet(girlGObj, 0.0f, -1300.0f, -1700.0f);
    scpPlayPosSet(boyGObj, 20.0f, -1300.0f, -1700.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, 572);
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(229);

    while (stage_CheckAnimationFrame(298, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_4r);

    while (stage_CheckAnimationFinish(298) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(girlGObj, 514);
    scpPlayEnd(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    _ACTWait(1);

    iosOmSendMail(girlGObj, 60, boyGObj);

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
}

void actSt04rSekizoChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rSekizoChk_jun26(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04rSekizoChk_aug6(self);
        return;
    }
    if (ICO_NTSC) {
        actSt04rSekizoChk_ntsc(self);
        return;
    }
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           scpTriggerFloorAttr(boyGObj, 0x8000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x8000000) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        lt_switch_layout(51);
    } else if (ICO_AUG6JUN26) {
        scpAdpcmPlayRequestFunc(18, &sekizo5c, 1, 1, 1);
    } else {
        lt_switch_layout(PAL_JPNKORCN(55, 52));
    }

    if (ICO_AUG6JUN26) {
        while (sekizo5c == 0) {
            _ACTWait(1);
        }
    } else {
        scpBoyControlReadDisable = 1;
    }
    scpKillEnemyAll();

    scpMaskGeneratorAll();

    if (ICO_NTSC) {
        scpAdpcmPlayRequestFunc(18, &sekizo5c, 1, 1, 1);
    } else if (ICO_AUG6JUN26) {
        lt_switch_layout(51);
    } else {
        scpAdpcmPlayRequestFunc(18, &stair5d, 1, 1, 1);
    }

    if (ICO_NTSC) {
        while (sekizo5c == 0) {
            _ACTWait(1);
        }
    } else if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 1;
    } else {
        while (stair5d == 0) {
            _ACTWait(1);
        }
    }

    SetWayGroupActive(1, 1);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(331, 327, 298, 297), 1, 0);

    if (ICO_NTSCAUG6JUN26) {
        sekizo_4r = iosPadActRequest(boyPad, 9);
        sekizo_4r_vol = 128;
        iosPadActVolumeSet(sekizo_4r, 128);
    } else {
        st04r_yure = iosPadActRequest(boyPad, 9);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);
    }

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, PAL_NTSCAUG6JUN26(532, 514));

    scpPlayPosSet(girlGObj, 0.0f, -1300.0f, -1700.0f);
    scpPlayPosSet(boyGObj, 20.0f, -1300.0f, -1700.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, PAL_NTSC_AUG6JUN26(645, 572, 573));
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(PAL_NTSCAUG6JUN26(254, 229));

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(331, 327, 298, 297), 151, 0) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(PAL_NTSCAUG6JUN26(st04r_yure, sekizo_4r));

    if (ICO_NTSC) {
        while (stage_CheckAnimationFinish(298) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpPlayMot(girlGObj, 514);
    } else if (ICO_AUG6JUN26) {
        scpPlayMot(girlGObj, 514);
    } else {
        while (stage_CheckAnimationFinish(PAL_JPNKORCN(331, 327)) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpPlayMot(girlGObj, 532);
    }
    scpPlayEnd(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    if (!ICO_AUG6JUN26) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        iosOmSendMail(girlGObj, 60, boyGObj);

        lt_switch_layout(50);
    } else if (ICO_AUG6JUN26) {
        lt_switch_layout(50);
    } else {
        iosOmSendMail(girlGObj, 63, boyGObj);

        lt_switch_layout(PAL_JPNKORCN(54, 51));
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rRope1Chk_jun26(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(227) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1289))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1289)->active = 0;

            gflagOn(227);

            stage_SetAnimation(260, 1, 0);

            while (stage_CheckAnimationFinish(260) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

void actSt04rRope1Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rRope1Chk_jun26(self);
        return;
    }
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(PAL_NTSCAUG6JUN26(261, 236)) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1459, 1355, 1351)))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(324, 320, 291, 290), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(324, 320, 291, 290)) ==
                   0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1459, 1355, 1351))->active = 0;

            gflagOn(PAL_NTSCAUG6JUN26(261, 236));

            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(320, 316, 287, 286), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(320, 316, 287, 286)) ==
                   0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

static void actSt04rRope2Chk_jun26(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(228) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1290))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1290)->active = 0;

            gflagOn(228);

            stage_SetAnimation(261, 1, 0);

            while (stage_CheckAnimationFinish(261) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

void actSt04rRope2Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rRope2Chk_jun26(self);
        return;
    }
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(PAL_NTSCAUG6JUN26(262, 237)) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1460, 1356, 1352)))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(325, 321, 292, 291), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(325, 321, 292, 291)) ==
                   0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1460, 1356, 1352))->active = 0;

            gflagOn(PAL_NTSCAUG6JUN26(262, 237));

            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(321, 317, 288, 287), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(321, 317, 288, 287)) ==
                   0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

void actSt04rBrgCommon(GObj *volatile self)
{
    GProc *th;

    lt_switch_layout(PAL_JPNKORCN(55, 52));

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(86, &sekizo5c, 1, 1, 0);

    th = actCreateSubThread(actSt04rBrg1Sub, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&((GProc *)th)->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&sekizo5c, 256);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(sekizo_4r, 0, -1);

        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt04rRope3Chk_jun26(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(229) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1291))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1291)->active = 0;

            gflagOn(229);

            stage_SetAnimation(262, 1, 0);

            while (stage_CheckAnimationFinish(262) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

void actSt04rRope3Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rRope3Chk_jun26(self);
        return;
    }
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(PAL_NTSCAUG6JUN26(263, 238)) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1461, 1357, 1353)))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(326, 322, 293, 292), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(326, 322, 293, 292)) ==
                   0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1461, 1357, 1353))->active = 0;

            gflagOn(PAL_NTSCAUG6JUN26(263, 238));

            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(322, 318, 289, 288), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(322, 318, 289, 288)) ==
                   0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

static void actSt04rRope4Chk_jun26(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(230) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1292))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1292)->active = 0;

            gflagOn(230);

            stage_SetAnimation(263, 1, 0);

            while (stage_CheckAnimationFinish(263) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

void actSt04rRope4Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rRope4Chk_jun26(self);
        return;
    }
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(PAL_NTSCAUG6JUN26(264, 239)) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1462, 1358, 1354)))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(327, 323, 294, 293), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(327, 323, 294, 293)) ==
                   0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1462, 1358, 1354))->active = 0;

            gflagOn(PAL_NTSCAUG6JUN26(264, 239));

            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(323, 319, 290, 289), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(323, 319, 290, 289)) ==
                   0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

static void actSt04rMonyou01Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(249);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(304, 1, 0);

    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(304, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou01Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    if (ICO_JUN26) {
        gflagOn(240);
    } else {
        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 514);

        gflagOn(249);

        scpSleepEnemyAll();
    }
    _ACTWait(5);

    stage_SetAnimation(PAL_JUN26(303, 271), 1, 0);

    soundSeDefPlay(PAL_JUN26(1308, 1277), 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JUN26(303, 271), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }

    scpBoyControlReadDisable = 0;
}

void actSt04rMonyou01Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rMonyou01Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rMonyou01Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(274);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(337, 333), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(337, 333), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou02Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(250);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(305, 1, 0);

    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(305, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou02Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    if (ICO_JUN26) {
        gflagOn(241);
    } else {
        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 514);

        gflagOn(250);

        scpSleepEnemyAll();
    }
    _ACTWait(5);

    stage_SetAnimation(PAL_JUN26(304, 272), 1, 0);

    soundSeDefPlay(PAL_JUN26(1308, 1277), 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JUN26(304, 272), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }

    scpBoyControlReadDisable = 0;
}

void actSt04rMonyou02Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rMonyou02Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rMonyou02Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(275);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(338, 334), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(338, 334), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou03Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x3000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(251);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(306, 1, 0);

    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(306, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou03Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x3000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    if (ICO_JUN26) {
        gflagOn(242);
    } else {
        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 514);

        gflagOn(251);

        scpSleepEnemyAll();
    }
    _ACTWait(5);

    stage_SetAnimation(PAL_JUN26(305, 273), 1, 0);

    soundSeDefPlay(PAL_JUN26(1308, 1277), 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JUN26(305, 273), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }

    scpBoyControlReadDisable = 0;
}

void actSt04rMonyou03Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rMonyou03Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rMonyou03Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x3000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(276);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(339, 335), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(339, 335), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou04Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(252);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(307, 1, 0);

    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(307, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou04Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    if (ICO_JUN26) {
        gflagOn(243);
    } else {
        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 514);

        gflagOn(252);

        scpSleepEnemyAll();
    }
    _ACTWait(5);

    stage_SetAnimation(PAL_JUN26(306, 274), 1, 0);

    soundSeDefPlay(PAL_JUN26(1308, 1277), 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JUN26(306, 274), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }

    scpBoyControlReadDisable = 0;
}

void actSt04rMonyou04Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rMonyou04Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rMonyou04Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(277);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(340, 336), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(340, 336), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou05Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x5000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(253);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(308, 1, 0);

    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(308, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou05Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x5000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    if (ICO_JUN26) {
        gflagOn(244);
    } else {
        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 514);

        gflagOn(253);

        scpSleepEnemyAll();
    }
    _ACTWait(5);

    stage_SetAnimation(PAL_JUN26(307, 275), 1, 0);

    soundSeDefPlay(PAL_JUN26(1308, 1277), 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JUN26(307, 275), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }

    scpBoyControlReadDisable = 0;
}

void actSt04rMonyou05Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rMonyou05Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rMonyou05Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x5000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(278);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(341, 337), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(341, 337), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou06Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x6000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(254);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(309, 1, 0);

    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(309, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou06Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x6000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    if (ICO_JUN26) {
        gflagOn(245);
    } else {
        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 514);

        gflagOn(254);

        scpSleepEnemyAll();
    }
    _ACTWait(5);

    stage_SetAnimation(PAL_JUN26(308, 276), 1, 0);

    soundSeDefPlay(PAL_JUN26(1308, 1277), 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JUN26(308, 276), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }

    scpBoyControlReadDisable = 0;
}

void actSt04rMonyou06Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rMonyou06Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rMonyou06Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x6000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(279);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(342, 338), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(342, 338), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou07Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x7000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x7000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(255);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(310, 1, 0);

    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(310, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04rMonyou07Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x7000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x7000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    if (ICO_JUN26) {
        gflagOn(246);
    } else {
        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 514);

        gflagOn(255);

        scpSleepEnemyAll();
    }
    _ACTWait(5);

    stage_SetAnimation(PAL_JUN26(309, 277), 1, 0);

    soundSeDefPlay(PAL_JUN26(1308, 1277), 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JUN26(309, 277), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (ICO_JUN26) {
        SetWayGroupActive(1, 1);
    } else {
        scpWakeupEnemyAll();
    }

    scpBoyControlReadDisable = 0;
}

void actSt04rMonyou07Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rMonyou07Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rMonyou07Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x7000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x7000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(280);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(343, 339), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(343, 339), 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

void actSt04rMonyou01Chk(GObj *volatile self);
void actSt04rMonyou02Chk(GObj *volatile self);
void actSt04rMonyou03Chk(GObj *volatile self);
void actSt04rMonyou04Chk(GObj *volatile self);
void actSt04rMonyou05Chk(GObj *volatile self);
void actSt04rMonyou06Chk(GObj *volatile self);

static void actSt04rBarricadeChk_ntsc(GObj *volatile self)
{
    GObj *n;

    while ((n = scpIsBombExplode(19)) == 0 || scpTriggerBall(self, n, 350.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(240);
    _ACTWait(5);

    stage_SetAnimation(311, 1, 0);

    while (stage_CheckAnimationFinish(311) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

static void actSt04rBarricadeChk_aug6jun26(GObj *volatile self)
{
    GObj *n;

    while ((n = scpIsBombExplode(19)) == 0 ||
           scpTriggerBall(self, n, PAL_JUN26(350.0f, 500.0f)) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    if (!ICO_JUN26) {
        scpSleepEnemyAll();
    }

    gflagOn(PAL_JUN26(240, 231));
    _ACTWait(5);

    stage_SetAnimation(PAL_JUN26(310, 278), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JUN26(310, 278)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

void actSt04rBarricadeChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rBarricadeChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rBarricadeChk_aug6jun26(self);
        return;
    }
    GObj *n;

    while ((n = scpIsBombExplode(19)) == 0 || scpTriggerBall(self, n, 350.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(265);
    _ACTWait(5);

    stage_SetAnimation(PAL_JPNKORCN(344, 340), 1, 0);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(344, 340)) == 0) {
        if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKORCN(344, 340), 0, -1);

            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 3);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt04rGondolaChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    _ACTWait(15);

    if (gflagChk(257) != 0) {
        stage_SetAnimation(201, 1, 200);

        scpWakeupItemWithBoundary(-675.0f, -150.0f, -1600.0f, 200.0f);

        while (stage_CheckAnimationFrame(201, 220, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1297, 0, 0, 1);

        while (stage_CheckAnimationFrame(201, 240, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1298, 0, 0, 1);

        while (stage_CheckAnimationFrame(201, 250, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1295, 0, 0, 1);

        while (stage_CheckAnimationFrame(201, 415, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1296, 0, 0, 1);

        while (stage_CheckAnimationFinish(201) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOff(257);
    } else {
        stage_SetAnimation(201, 1, 0);

        scpWakeupItemWithBoundary(-675.0f, -1200.0f, -1600.0f, 200.0f);

        soundSeDefPlay(1295, 0, 0, 1);

        while (stage_CheckAnimationFrame(201, 145, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1296, 0, 0, 1);
        soundSeDefPlay(1297, 0, 0, 1);

        while (stage_CheckAnimationFrame(201, 170, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1298, 0, 0, 1);

        while (stage_CheckAnimationFrame(201, 200, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOn(257);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);

    scpWakeupEnemyAll();

    gondola_chk_mes[0].func = actSt04rGondolaCharaChk;
    sub->mail = gondola_chk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt04rGondolaChk_jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;

    _ACTWait(15);

    if (gflagChk(248) != 0) {
        stage_SetAnimation(180, 1, 200);

        scpWakeupItemWithBoundary(-675.0f, -150.0f, -1600.0f, 200.0f);

        soundSeDefPlay(1266, 0, 0, 1);

        while (stage_CheckAnimationFinish(180) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1267, 0, 0, 1);

        gflagOff(248);
    } else {
        stage_SetAnimation(180, 1, 0);

        scpWakeupItemWithBoundary(-675.0f, -1200.0f, -1600.0f, 200.0f);

        soundSeDefPlay(1266, 0, 0, 1);

        while (stage_CheckAnimationFrame(180, 200, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1267, 0, 0, 1);

        gflagOn(248);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);

    gondola_chk_mes[0].func = actSt04rGondolaCharaChk;
    sub->mail = gondola_chk_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

static void actSt04rGondolaChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    _ACTWait(15);

    if (gflagChk(257) != 0) {
        stage_SetAnimation(200, 1, 200);

        scpWakeupItemWithBoundary(-675.0f, -150.0f, -1600.0f, 200.0f);

        while (stage_CheckAnimationFrame(200, 220, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1297, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 240, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1298, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 250, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1295, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 415, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1296, 0, 0, 1);

        while (stage_CheckAnimationFinish(200) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOff(257);
    } else {
        stage_SetAnimation(200, 1, 0);

        scpWakeupItemWithBoundary(-675.0f, -1200.0f, -1600.0f, 200.0f);

        soundSeDefPlay(1295, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 145, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1296, 0, 0, 1);
        soundSeDefPlay(1297, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 170, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1298, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 200, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOn(257);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);

    scpWakeupEnemyAll();

    gondola_chk_mes[0].func = actSt04rGondolaCharaChk;
    sub->mail = gondola_chk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt04rGondolaChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rGondolaChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt04rGondolaChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04rGondolaChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    _ACTWait(15);

    if (gflagChk(282) != 0) {
        stage_SetAnimation(PAL_JPNKORCN(229, 225), 1, 200);

        scpWakeupItemWithBoundary(-675.0f, -150.0f, -1600.0f, 200.0f);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(229, 225), 220, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1321, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(229, 225), 240, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1322, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(229, 225), 250, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1319, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(229, 225), 415, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1320, 0, 0, 1);

        while (stage_CheckAnimationFinish(PAL_JPNKORCN(229, 225)) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOff(282);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(229, 225), 1, 0);

        scpWakeupItemWithBoundary(-675.0f, -1200.0f, -1600.0f, 200.0f);

        SetGirlDangerGObj(boyGObj);

        soundSeDefPlay(1319, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(229, 225), 145, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1320, 0, 0, 1);
        soundSeDefPlay(1321, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(229, 225), 170, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1322, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(229, 225), 200, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        ClearGirlDangerGObj();

        gflagOn(282);
    }

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0xA000000) == 0) {
        gflagOn(286);
    } else {
        gflagOff(286);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKORCN(54, 51));

    scpWakeupEnemyAll();

    gondola_chk_mes[0].func = actSt04rGondolaCharaChk;
    sub->mail = gondola_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04rSolarStageChange(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(234, 209, 200)) == 0) {
        solar_stage_change_mes[0].func = actSt04rSolarStageChangeChk;
        act->mail = solar_stage_change_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

static void actSt04rCrest01_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(209) == 0) {
        stage_SetAnimation(249, 0, 0);

        crest01_mes[0].func = actSt04rCrestMain;
        act->mail = crest01_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(249, 0, -1);
    }
}

void actSt04rCrest01(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rCrest01_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(243, 218)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(309, 305, 276, 275), 0, 0);

        crest01_mes[0].func = actSt04rCrestMain;
        act->mail = crest01_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(309, 305, 276, 275), 0, -1);
        scpBoyControlReadDisable = 0;
    }
}

static void actSt04rCrest03_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(211) == 0) {
        stage_SetAnimation(253, 0, 0);

        crest03_mes[0].func = actSt04rCrest3Main;
        act->mail = crest03_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(253, 0, -1);
    }
}

void actSt04rCrest03(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rCrest03_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(245, 220)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(313, 309, 280, 279), 0, 0);

        crest03_mes[0].func = actSt04rCrest3Main;
        act->mail = crest03_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(313, 309, 280, 279), 0, -1);
        scpBoyControlReadDisable = 0;
    }
}

void actSt04rC1Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(250, 225, 216)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1404, 1304, 1300, 1239))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1405, 1305, 1301, 1240))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(317, 313, 284, 283, 257), 0, 0);

        c1_ball_mes[0].func = actSt04rC1BallMain;
        act->mail = c1_ball_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(317, 313, 284, 283, 257), 0, -1);
    }
}

void actSt04rC2Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(251, 226, 217)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1487, 1382, 1376, 1315))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1488, 1383, 1377, 1316))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(318, 314, 285, 284, 258), 0, 0);

        c2_ball_mes[0].func = actSt04rC2BallMain;
        act->mail = c2_ball_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(318, 314, 285, 284, 258), 0, -1);
    }
}

void actSt04rC3Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(252, 227, 218)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1489, 1384, 1378, 1317))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1490, 1385, 1379, 1318))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(319, 315, 286, 285, 259), 0, 0);

        c3_ball_mes[0].func = actSt04rC3BallMain;
        act->mail = c3_ball_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(319, 315, 286, 285, 259), 0, -1);
    }
}

void actSt04rStair(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(253, 228, 219)) == 0) {
        if (girlGObj != 0) {
            stair_mes[0].func = actSt04rStairChk;
            act->mail = stair_mes;
            ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
            _ACTWait(0);
        }
    } else {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1523, 1420, 1414, 1348))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(351, 347, 318, 317, 285), -1, -2);
    }
}

void actSt04rBrg1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(259, 234, 225)) == 0) {
        brg1_mes[0].func = actSt04rBrg1Chk;
        act->mail = brg1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt04rBrg2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(260, 235, 226)) == 0) {
        brg2_mes[0].func = actSt04rBrg2Chk;
        act->mail = brg2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt04rBrg1Way(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(259, 234, 225)) == 0) {
        brg1_way_mes[0].func = actSt04rBrg1WayChk;
        act->mail = brg1_way_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt04rBrg2Way(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(260, 235, 226)) == 0) {
        brg2_way_mes[0].func = actSt04rBrg2WayChk;
        act->mail = brg2_way_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt04rRope1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(261, 236, 227)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(320, 316, 287, 286, 260), 0, 0);

        rope1_mes[0].func = actSt04rRope1Chk;
        act->mail = rope1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(320, 316, 287, 286, 260), 0, -1);

        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1459, 1355, 1351, 1289))->active = 0;
    }
}

void actSt04rRope2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(262, 237, 228)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(321, 317, 288, 287, 261), 0, 0);

        rope2_mes[0].func = actSt04rRope2Chk;
        act->mail = rope2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(321, 317, 288, 287, 261), 0, -1);

        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1460, 1356, 1352, 1290))->active = 0;
    }
}

void actSt04rRope3(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(263, 238, 229)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(322, 318, 289, 288, 262), 0, 0);

        rope3_mes[0].func = actSt04rRope3Chk;
        act->mail = rope3_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(322, 318, 289, 288, 262), 0, -1);

        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1461, 1357, 1353, 1291))->active = 0;
    }
}

void actSt04rRope4(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(264, 239, 230)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(323, 319, 290, 289, 263), 0, 0);

        rope4_mes[0].func = actSt04rRope4Chk;
        act->mail = rope4_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(323, 319, 290, 289, 263), 0, -1);

        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1462, 1358, 1354, 1292))->active = 0;
    }
}

void actSt04rSekizo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(254, 229, 220)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(331, 327, 298, 297, 267), 0, 0);

        sekizo_mes[0].func = actSt04rSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(331, 327, 298, 297, 267), 0, -1);
    }
}

void actSt04rBarricade(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(265, 240, 231)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(344, 340, 311, 310, 278), 0, 0);

        barricade_mes[0].func = actSt04rBarricadeChk;
        act->mail = barricade_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(344, 340, 311, 310, 278), 0, -1);
    }
}

void actSt04rGondola(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(282, 257, 248)) != 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(229, 225, 201, 200, 180), 0, 200);

        gondola_mes[0].func = actSt04rGondolaChk;
        act->mail = gondola_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(229, 225, 201, 200, 180), 0, 0);

        gondola2_mes[0].func = actSt04rGondolaChk;
        act->mail = gondola2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt04rCrest01XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(243, 218, 209)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(314, 310, 281, 280, 254), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(314, 310, 281, 280, 254), 0, -1);
    }
}

void actSt04rCrest02XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(244, 219, 210)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(315, 311, 282, 281, 255), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(315, 311, 282, 281, 255), 0, -1);
    }
}

void actSt04rCrest03XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(245, 220, 211)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(316, 312, 283, 282, 256), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(316, 312, 283, 282, 256), 0, -1);
    }
}

void actSt04rC2BallXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(251, 226, 217)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(318, 314, 285, 284, 258), 0, 0);

        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1408, 1308, 1304, 1243))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1409, 1309, 1305, 1244))->active = 0;
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(318, 314, 285, 284, 258), 0, -1);
    }
}

void actSt04rC3BallXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(252, 227, 218)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(319, 315, 286, 285, 259), 0, 0);

        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1562, 1459, 1453, 1388))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1563, 1460, 1454, 1389))->active = 0;
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(319, 315, 286, 285, 259), 0, -1);
    }
}

static void actSt04rTorch1_1_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(232) == 0) {
        torch1_1_mes[0].func = actSt04rTorch1_1Chk;
        act->mail = torch1_1_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        scpTorchLightOn(1239);
    }
}

void actSt04rTorch1_1(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rTorch1_1_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(266, 241)) == 0) {
        torch1_1_mes[0].func = actSt04rTorch1_1Chk;
        act->mail = torch1_1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (ICO_NTSC) {
            scpTorchLightOn(1304);
        } else if (ICO_AUG6JUN26) {
            scpTorchLightOn(1300);
        } else {
            scpTorchLightOn(1404);
            stage_SetAnimation(PAL_JPNKORCN(210, 206), 0, -1);
        }
    }
}

static void actSt04rTorch1_2_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(233) == 0) {
        torch1_2_mes[0].func = actSt04rTorch1_2Chk;
        act->mail = torch1_2_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        scpTorchLightOn(1240);
    }
}

void actSt04rTorch1_2(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rTorch1_2_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(267, 242)) == 0) {
        torch1_2_mes[0].func = actSt04rTorch1_2Chk;
        act->mail = torch1_2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (ICO_NTSC) {
            scpTorchLightOn(1305);
        } else if (ICO_AUG6JUN26) {
            scpTorchLightOn(1301);
        } else {
            scpTorchLightOn(1405);
            stage_SetAnimation(PAL_JPNKORCN(211, 207), 0, -1);
        }
    }
}

void actSt04rTorch2_1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(268, 243, 234)) != 0 ||
        gflagChk(PAL_NTSCAUG6_JUN26(270, 245, 236)) != 0) {
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(1487, 1382, 1376, 1315));
    } else {
        torch2_1_mes[0].func = actSt04rTorch2_1Chk;
        act->mail = torch2_1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt04rTorch2_2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(269, 244, 235)) != 0 ||
        gflagChk(PAL_NTSCAUG6_JUN26(271, 246, 237)) != 0) {
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(1488, 1383, 1377, 1316));
    } else {
        torch2_2_mes[0].func = actSt04rTorch2_2Chk;
        act->mail = torch2_2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt04rTorch3_1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(272, 247, 238)) == 0) {
        torch3_1_mes[0].func = actSt04rTorch3_1Chk;
        act->mail = torch3_1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(1489, 1384, 1378, 1317));
    }
}

void actSt04rTorch3_2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(273, 248, 239)) == 0) {
        torch3_2_mes[0].func = actSt04rTorch3_2Chk;
        act->mail = torch3_2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(1490, 1385, 1379, 1318));
    }
}

static void actSt04rTorch2_1XL_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(243) != 0 || gflagChk(245) != 0) {
        scpTorchLightOn(1308);
    } else {
        torch2_1xl_mes[0].func = actSt04rTorch2_1XLChk;
        act->mail = torch2_1xl_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt04rTorch2_1XL_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_JUN26(243, 234)) != 0 || gflagChk(PAL_JUN26(245, 236)) != 0) {
        scpTorchLightOn(PAL_JUN26(1304, 1243));
    } else {
        torch2_1xl_mes[0].func = actSt04rTorch2_1XLChk;
        act->mail = torch2_1xl_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    }
}

void actSt04rTorch2_1XL(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rTorch2_1XL_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rTorch2_1XL_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(268) != 0 || gflagChk(270) != 0) {
        scpTorchLightOn(1408);
        stage_SetAnimation(PAL_JPNKORCN(212, 208), 0, -1);
    } else {
        torch2_1xl_mes[0].func = actSt04rTorch2_1XLChk;
        act->mail = torch2_1xl_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt04rTorch2_2XL_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(244) != 0 || gflagChk(246) != 0) {
        scpTorchLightOn(1309);
    } else {
        torch2_2xl_mes[0].func = actSt04rTorch2_2XLChk;
        act->mail = torch2_2xl_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt04rTorch2_2XL_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_JUN26(244, 235)) != 0 || gflagChk(PAL_JUN26(246, 237)) != 0) {
        scpTorchLightOn(PAL_JUN26(1305, 1244));
    } else {
        torch2_2xl_mes[0].func = actSt04rTorch2_2XLChk;
        act->mail = torch2_2xl_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    }
}

void actSt04rTorch2_2XL(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rTorch2_2XL_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rTorch2_2XL_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(269) != 0 || gflagChk(271) != 0) {
        scpTorchLightOn(1409);
        stage_SetAnimation(PAL_JPNKORCN(213, 209), 0, -1);
    } else {
        torch2_2xl_mes[0].func = actSt04rTorch2_2XLChk;
        act->mail = torch2_2xl_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rTorch3_1XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(272, 247, 238)) != 0) {
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(1562, 1459, 1453, 1388));
    }
}

void actSt04rTorch3_2XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(273, 248, 239)) != 0) {
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(1563, 1460, 1454, 1389));
    }
}

void actSt04rMonyou01(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(274, 249, 240)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(337, 333, 304, 303, 271), 0, 0);

        monyou01_mes[0].func = actSt04rMonyou01Chk;
        act->mail = monyou01_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(337, 333, 304, 303, 271), 0, -1);
    }
}

void actSt04rMonyou02(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(275, 250, 241)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(338, 334, 305, 304, 272), 0, 0);

        monyou02_mes[0].func = actSt04rMonyou02Chk;
        act->mail = monyou02_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(338, 334, 305, 304, 272), 0, -1);
    }
}

void actSt04rMonyou03(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(276, 251, 242)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(339, 335, 306, 305, 273), 0, 0);

        monyou03_mes[0].func = actSt04rMonyou03Chk;
        act->mail = monyou03_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(339, 335, 306, 305, 273), 0, -1);
    }
}

void actSt04rMonyou04(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(277, 252, 243)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(340, 336, 307, 306, 274), 0, 0);

        monyou04_mes[0].func = actSt04rMonyou04Chk;
        act->mail = monyou04_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(340, 336, 307, 306, 274), 0, -1);
    }
}

void actSt04rMonyou05(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(278, 253, 244)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(341, 337, 308, 307, 275), 0, 0);

        monyou05_mes[0].func = actSt04rMonyou05Chk;
        act->mail = monyou05_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(341, 337, 308, 307, 275), 0, -1);
    }
}

void actSt04rMonyou06(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(279, 254, 245)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(342, 338, 309, 308, 276), 0, 0);

        monyou06_mes[0].func = actSt04rMonyou06Chk;
        act->mail = monyou06_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(342, 338, 309, 308, 276), 0, -1);
    }
}

void actSt04rMonyou07(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(280, 255, 246)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(343, 339, 310, 309, 277), 0, 0);

        monyou07_mes[0].func = actSt04rMonyou07Chk;
        act->mail = monyou07_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(343, 339, 310, 309, 277), 0, -1);
    }
}

void actSt05cDoorInit(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(165, 148, 141)) != 0 &&
        gflagChk(PAL_NTSCAUG6_JUN26(234, 209, 200)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(347, 343, 314, 313, 281), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6_JUN26(165, 148, 141)) == 0 ||
        gflagChk(PAL_NTSCAUG6_JUN26(234, 209, 200)) != 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(347, 343, 314, 313, 281), 0, 0);
    }
}

void actSt04rC1BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 0;
    }

    sub->mainMail = c1_ball_main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04rC1BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c1_ball_switch_mes[0].func = actSt04rC1BallTurn;
    sub->mail = c1_ball_switch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actSt04rC1BallTurn_jun26(GObj *volatile self)
{
    lt_switch_layout(51);

    scpSleepEnemyAll();

    gflagOn(216);
    _ACTWait(60);

    stage_SetAnimation(257, 1, 0);
    scpAdpcmPlayRequestFunc(75, 0, 1, 1, 1);

    while (stage_CheckAnimationFinish(257) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(1239)->active = 1;
    scpSearchGobj(1240)->active = 1;

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

void actSt04rC1BallTurn(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rC1BallTurn_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        lt_switch_layout(51);

        scpSleepEnemyAll();

        gflagOn(225);
        _ACTWait(60);

        scpAdpcmPlayRequestFunc(80, &ball1_4r, 1, 1, 1);

        while (ball1_4r == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(284, 1, 0);

        while (stage_CheckAnimationFinish(284) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpSearchGobj(1304)->active = 1;
        scpSearchGobj(1305)->active = 1;

        _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

        scpWakeupEnemyAll();

        scpBoyControlReadDisable = 0;

        lt_switch_layout(50);
    } else if (ICO_AUG6JUN26) {
        lt_switch_layout(51);

        scpSleepEnemyAll();

        gflagOn(225);
        _ACTWait(60);

        scpAdpcmPlayRequestFunc(80, &ball1_4r, 1, 1, 1);

        while (ball1_4r == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(283, 1, 0);

        while (stage_CheckAnimationFinish(283) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpSearchGobj(1300)->active = 1;
        scpSearchGobj(1301)->active = 1;

        _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

        scpWakeupEnemyAll();

        scpBoyControlReadDisable = 0;

        lt_switch_layout(50);
    } else {
        turnBall(self, 250, PAL_JPNKORCN(317, 313), 1404, 1405);
    }
}

void actSt04rC2BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 0;
    }

    sub->mainMail = c2_ball_main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04rC2BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c2_ball_switch_mes[0].func = actSt04rC2BallTurn;
    sub->mail = c2_ball_switch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actSt04rC2BallTurn_jun26(GObj *volatile self)
{
    lt_switch_layout(51);

    scpSleepEnemyAll();

    gflagOn(217);
    _ACTWait(60);

    stage_SetAnimation(258, 1, 0);
    scpAdpcmPlayRequestFunc(75, 0, 1, 1, 1);

    while (stage_CheckAnimationFinish(258) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(1315)->active = 1;
    scpSearchGobj(1316)->active = 1;

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

void actSt04rC2BallTurn(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rC2BallTurn_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        lt_switch_layout(51);

        scpSleepEnemyAll();

        gflagOn(226);
        _ACTWait(60);

        scpAdpcmPlayRequestFunc(80, &ball2_4r, 1, 1, 1);

        while (ball2_4r == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(285, 1, 0);

        while (stage_CheckAnimationFinish(285) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpSearchGobj(1382)->active = 1;
        scpSearchGobj(1383)->active = 1;

        _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

        scpWakeupEnemyAll();

        scpBoyControlReadDisable = 0;

        lt_switch_layout(50);
    } else if (ICO_AUG6JUN26) {
        lt_switch_layout(51);

        scpSleepEnemyAll();

        gflagOn(226);
        _ACTWait(60);

        scpAdpcmPlayRequestFunc(80, &ball2_4r, 1, 1, 1);

        while (ball2_4r == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(284, 1, 0);

        while (stage_CheckAnimationFinish(284) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpSearchGobj(1376)->active = 1;
        scpSearchGobj(1377)->active = 1;

        _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

        scpWakeupEnemyAll();

        scpBoyControlReadDisable = 0;

        lt_switch_layout(50);
    } else {
        turnBall(self, 251, PAL_JPNKORCN(318, 314), 1487, 1488);
    }
}

void actSt04rC3BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 0;
    }

    sub->mainMail = c3_ball_main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04rC3BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c3_ball_switch_mes[0].func = actSt04rC3BallTurn;
    sub->mail = c3_ball_switch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actSt04rC3BallTurn_jun26(GObj *volatile self)
{
    lt_switch_layout(51);

    scpSleepEnemyAll();

    gflagOn(218);
    _ACTWait(60);

    stage_SetAnimation(259, 1, 0);
    scpAdpcmPlayRequestFunc(75, 0, 1, 1, 1);

    while (stage_CheckAnimationFinish(259) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(1317)->active = 1;
    scpSearchGobj(1318)->active = 1;

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

void actSt04rC3BallTurn(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rC3BallTurn_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        lt_switch_layout(51);

        scpSleepEnemyAll();

        gflagOn(227);
        _ACTWait(60);

        scpAdpcmPlayRequestFunc(80, &ball3_4r, 1, 1, 1);

        while (ball3_4r == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(286, 1, 0);

        while (stage_CheckAnimationFinish(286) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpSearchGobj(1384)->active = 1;
        scpSearchGobj(1385)->active = 1;

        scpWakeupEnemyAll();

        scpBoyControlReadDisable = 0;

        lt_switch_layout(50);
    } else if (ICO_AUG6JUN26) {
        lt_switch_layout(51);

        scpSleepEnemyAll();

        gflagOn(227);
        _ACTWait(60);

        scpAdpcmPlayRequestFunc(80, &ball3_4r, 1, 1, 1);

        while (ball3_4r == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(285, 1, 0);

        while (stage_CheckAnimationFinish(285) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpSearchGobj(1378)->active = 1;
        scpSearchGobj(1379)->active = 1;

        scpWakeupEnemyAll();

        scpBoyControlReadDisable = 0;

        lt_switch_layout(50);
    } else {
        turnBall(self, 252, PAL_JPNKORCN(319, 315), 1489, 1490);
    }
}

static void actSt04rCrestMain_jun26(GObj *volatile self)
{
    while (scpIsTorchLightOn(1239) == 0 || scpIsTorchLightOn(1240) == 0 || gflagChk(216) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(29, &crest1_4r, 0, 1, 1);

    while (crest1_4r == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(249, 1, 0);

    while (stage_CheckAnimationFrame(249, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    st04r_yure = iosPadActRequest(boyPad, 10);
    st04r_yure_vol = 128;
    iosPadActVolumeSet(st04r_yure, 128);

    while (stage_CheckAnimationFrame(249, 190, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st04r_yure);

    while (stage_CheckAnimationFinish(249) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
    _ACTWait(1);

    gflagOn(209);
    gflagOn(212);
    gflagOff(213);
    gflagOff(215);
    gflagOff(199);
}

void actSt04rCrestMain(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rCrestMain_jun26(self);
        return;
    }
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1404, 1304, 1300)) == 0 ||
           scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1405, 1305, 1301)) == 0 ||
           gflagChk(PAL_NTSCAUG6JUN26(250, 225)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        lt_switch_layout(51);

        scpBoyControlReadDisable = 1;
        scpSleepEnemyAll();

        scpAdpcmPlayRequestFunc(26, &crest1_4r, 0, 1, 1);

        while (crest1_4r == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(276, 1, 0);

        while (stage_CheckAnimationFrame(276, 30, 0) == 0) {
            _ACTWait(1);
        }
    } else if (ICO_AUG6JUN26) {
        lt_switch_layout(51);

        scpBoyControlReadDisable = 1;
        scpSleepEnemyAll();

        scpAdpcmPlayRequestFunc(26, &crest1_4r, 0, 1, 1);

        while (crest1_4r == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(275, 1, 0);

        while (stage_CheckAnimationFrame(275, 30, 0) == 0) {
            _ACTWait(1);
        }
    } else {
        gateAnim = PAL_JPNKORCN(309, 305);
        gateAdpcm = 27;
        openGate(self);
    }
    _ACTWait(1);

    if (ICO_NTSC) {
        st04r_yure = iosPadActRequest(boyPad, 10);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);

        while (stage_CheckAnimationFrame(276, 190, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        iosPadActStop(st04r_yure);

        while (stage_CheckAnimationFinish(276) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpWakeupEnemyAll();

        lt_switch_layout(50);

        scpBoyControlReadDisable = 0;
        _ACTWait(1);

        gflagOn(218);
        gflagOn(221);
        gflagOff(222);
        gflagOff(224);
        gflagOff(208);
    } else if (ICO_AUG6JUN26) {
        st04r_yure = iosPadActRequest(boyPad, 10);
        st04r_yure_vol = 128;
        iosPadActVolumeSet(st04r_yure, 128);

        while (stage_CheckAnimationFrame(275, 190, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        iosPadActStop(st04r_yure);

        while (stage_CheckAnimationFinish(275) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpWakeupEnemyAll();

        lt_switch_layout(50);

        scpBoyControlReadDisable = 0;
        _ACTWait(1);

        gflagOn(218);
        gflagOn(221);
        gflagOff(222);
        gflagOff(224);
        gflagOff(208);
    } else {
        gflagOn(243);
        gflagOn(246);
        gflagOff(247);
        gflagOff(248);
        gflagOff(249);
        gflagOff(233);
    }
}

void actSt04rSolarStageChangeChk(GObj *volatile self)
{
    while (gflagChk(PAL_NTSCAUG6_JUN26(243, 218, 209)) == 0 ||
           gflagChk(PAL_NTSCAUG6_JUN26(244, 219, 210)) == 0 ||
           gflagChk(PAL_NTSCAUG6_JUN26(245, 220, 211)) == 0 ||
           gflagChk(PAL_NTSCAUG6_JUN26(232, 207, 198)) == 0) {
        _ACTWait(1);
    }

    RequestStageChange(8, boyGObj, PAL_NTSCAUG6JUN26(0, girlGObj), PAL_JUN26(2.0f, 16.0f),
                       PAL_JUN26(8.0f, 16.0f));
}

void actSt04rStairEvent(int x)
{
    volatile int local = x;
}

void actSt04rRope1Event(int x)
{
    volatile int local = x;
}

void actSt04rRope2Event(int x)
{
    volatile int local = x;
}

void actSt04rBrg1Event(int x)
{
    volatile int local = x;
}

static void actSt04rBrg1Chk_ntsc(GObj *volatile self)
{
    while (gflagChk(236) == 0 || gflagChk(237) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(234);
    _ACTWait(60);

    scpAdpcmPlayRequestFunc(84, &st05d_hasi_ntscaug6jun26, 1, 1, 1);

    while (st05d_hasi_ntscaug6jun26 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(295, 1, 0);

    while (stage_CheckAnimationFrame(295, 75, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(295) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

static void actSt04rBrg1Chk_jun26(GObj *volatile self)
{
    while (gflagChk(227) == 0 || gflagChk(228) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(225);
    _ACTWait(60);

    scpAdpcmPlayRequestFunc(79, &st05d_hasi_ntscaug6jun26, 1, 1, 1);

    while (st05d_hasi_ntscaug6jun26 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(264, 1, 0);

    while (stage_CheckAnimationFrame(264, 120, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(264) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

static void actSt04rBrg1Chk_aug6(GObj *volatile self)
{
    while (gflagChk(236) == 0 || gflagChk(237) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(234);
    _ACTWait(60);

    scpAdpcmPlayRequestFunc(84, &st05d_hasi_ntscaug6jun26, 1, 1, 1);

    while (st05d_hasi_ntscaug6jun26 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(294, 1, 0);

    while (stage_CheckAnimationFrame(294, 75, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(294) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

void actSt04rBrg1Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rBrg1Chk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt04rBrg1Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04rBrg1Chk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(261) == 0 || gflagChk(262) == 0) {
        _ACTWait(1);
    }

    gflagOn(259);

    sekizo_4r = PAL_JPNKORCN(328, 324);

    brg1_chk_mes[0].func = actSt04rBrgCommon;
    sub->mail = brg1_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt04rBrg1Sub(GObj *volatile self)
{
    _ACTWait(60);

    while (sekizo5c == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(sekizo5c->stream);

    stage_SetAnimation(sekizo_4r, 1, 0);

    while (stage_CheckAnimationFrame(sekizo_4r, 75, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(sekizo_4r) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt04rRope3Event(int x)
{
    volatile int local = x;
}

void actSt04rRope4Event(int x)
{
    volatile int local = x;
}

void actSt04rBrg2Event(int x)
{
    volatile int local = x;
}

static void actSt04rBrg2Chk_ntsc(GObj *volatile self)
{
    while (gflagChk(238) == 0 || gflagChk(239) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(235);
    _ACTWait(60);

    scpAdpcmPlayRequestFunc(84, &st05d_hasi_ntscaug6jun26, 1, 1, 1);

    while (st05d_hasi_ntscaug6jun26 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(296, 1, 0);

    while (stage_CheckAnimationFrame(296, 75, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(296) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

static void actSt04rBrg2Chk_jun26(GObj *volatile self)
{
    while (gflagChk(229) == 0 || gflagChk(230) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(226);
    _ACTWait(60);

    scpAdpcmPlayRequestFunc(79, &st05d_hasi_ntscaug6jun26, 1, 1, 1);

    while (st05d_hasi_ntscaug6jun26 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(265, 1, 0);

    while (stage_CheckAnimationFrame(265, 120, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(265) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

static void actSt04rBrg2Chk_aug6(GObj *volatile self)
{
    while (gflagChk(238) == 0 || gflagChk(239) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(235);
    _ACTWait(60);

    scpAdpcmPlayRequestFunc(84, &st05d_hasi_ntscaug6jun26, 1, 1, 1);

    while (st05d_hasi_ntscaug6jun26 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(295, 1, 0);

    while (stage_CheckAnimationFrame(295, 75, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(295) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
}

void actSt04rBrg2Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rBrg2Chk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt04rBrg2Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04rBrg2Chk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(263) == 0 || gflagChk(264) == 0) {
        _ACTWait(1);
    }

    gflagOn(260);

    sekizo_4r = PAL_JPNKORCN(329, 325);

    brg2_chk_mes[0].func = actSt04rBrgCommon;
    sub->mail = brg2_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04rRope1Chk(GObj *volatile self);
void actSt04rRope2Chk(GObj *volatile self);
void actSt04rRope3Chk(GObj *volatile self);
void actSt04rRope4Chk(GObj *volatile self);

void actSt04rBrg1WayChk(GObj *volatile self)
{
    while (gflagChk(PAL_NTSCAUG6_JUN26(259, 234, 225)) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(3, 1);
}

void actSt04rBrg2WayChk(GObj *volatile self)
{
    while (gflagChk(PAL_NTSCAUG6_JUN26(259, 234, 225)) == 0 ||
           gflagChk(PAL_NTSCAUG6_JUN26(260, 235, 226)) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(4, 1);
}

void actSt04rMonyou01Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou02Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou03Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou04Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou05Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou06Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou07Event(int x)
{
    volatile int local = x;
}

void actSt04rBarricadeEvent(int x)
{
    volatile int local = x;
}

static void actSt04rGondolaCharaChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) != 0) {
        _ACTWait(1);
    }

    gondola_chara_chk_mes[0].func = actSt04rGondolaChk;
    sub->mail = gondola_chara_chk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt04rGondolaCharaChk_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) != 0) {
        _ACTWait(1);
    }

    gondola_chara_chk_mes[0].func = actSt04rGondolaChk;
    sub->mail = gondola_chara_chk_mes;
    ACTSendMailCorrect(self, PAL_JUN26(393, 377));
    _ACTWait(0);
}

void actSt04rGondolaCharaChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04rGondolaCharaChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04rGondolaCharaChk_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) != 0 &&
           (gflagChk(286) == 0 || scpTriggerFloorAttr(girlGObj, 0xA000000) == 0)) {
        _ACTWait(1);
    }

    gondola_chara_chk_mes[0].func = actSt04rGondolaChk;
    sub->mail = gondola_chara_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt04rTorch1_1Chk_jun26(GObj *volatile self)
{
    while (scpIsTorchLightOn(1239) == 0) {
        _ACTWait(1);
    }

    gflagOn(232);
}

void actSt04rTorch1_1Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rTorch1_1Chk_jun26(self);
        return;
    }
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1404, 1304, 1300)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(241);
    } else {
        gflagOn(266);
        stage_SetAnimation(PAL_JPNKORCN(210, 206), 1, 0);
    }
}

static void actSt04rTorch1_2Chk_jun26(GObj *volatile self)
{
    while (scpIsTorchLightOn(1240) == 0) {
        _ACTWait(1);
    }

    gflagOn(233);
}

void actSt04rTorch1_2Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rTorch1_2Chk_jun26(self);
        return;
    }
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1405, 1305, 1301)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(242);
    } else {
        gflagOn(267);
        stage_SetAnimation(PAL_JPNKORCN(211, 207), 1, 0);
    }
}

void actSt04rTorch2_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6_JUN26(1487, 1382, 1376, 1315)) == 0) {
        _ACTWait(1);
    }

    gflagOn(PAL_NTSCAUG6_JUN26(268, 243, 234));
}

void actSt04rTorch2_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6_JUN26(1488, 1383, 1377, 1316)) == 0) {
        _ACTWait(1);
    }

    gflagOn(PAL_NTSCAUG6_JUN26(269, 244, 235));
}

void actSt04rTorch3_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6_JUN26(1489, 1384, 1378, 1317)) == 0) {
        _ACTWait(1);
    }

    gflagOn(PAL_NTSCAUG6_JUN26(272, 247, 238));
}

void actSt04rTorch3_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6_JUN26(1490, 1385, 1379, 1318)) == 0) {
        _ACTWait(1);
    }

    gflagOn(PAL_NTSCAUG6_JUN26(273, 248, 239));
}

static void actSt04rTorch2_1XLChk_jun26(GObj *volatile self)
{
    while (scpIsTorchLightOn(1243) == 0) {
        _ACTWait(1);
    }

    gflagOn(236);
}

void actSt04rTorch2_1XLChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rTorch2_1XLChk_jun26(self);
        return;
    }
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1408, 1308, 1304)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(245);
    } else {
        gflagOn(270);
        stage_SetAnimation(PAL_JPNKORCN(212, 208), 1, 0);
    }
}

static void actSt04rTorch2_2XLChk_jun26(GObj *volatile self)
{
    while (scpIsTorchLightOn(1244) == 0) {
        _ACTWait(1);
    }

    gflagOn(237);
}

void actSt04rTorch2_2XLChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rTorch2_2XLChk_jun26(self);
        return;
    }
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1409, 1309, 1305)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(246);
    } else {
        gflagOn(271);
        stage_SetAnimation(PAL_JPNKORCN(213, 209), 1, 0);
    }
}
