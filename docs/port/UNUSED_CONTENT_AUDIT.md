# Unused content audit

What each retail disc holds that the game never reaches, read from the disc images by
`tools/unused_content_audit.py` (the directories and the boot program's tables only).  This
page is generated: one section per disc, and the ctests `unused_content_audit_<disc>` compare each
section with the disc.  To regenerate it, with every image at hand:

    python3 -I tools/unused_content_audit.py --write

or one disc's section (the others kept): `python3 -I tools/unused_content_audit.py --slug jpn --write`.

How each part is decided is in the script's header.  A stage row is *reached* when it is a root
(the boot's logo; the title's attract movie, by `ico2/script/src/op.c`) or an exit row of a
reached stage leads to it; the debug Stage Select reaches every row whose pack is on the disc,
and is not counted.  An exit row no stage lists is an entrance row (where a forced switch lands:
`getExitIndexOfStage` in `ico2/fumi/src/act-game.c`), so it does not make its stage reached.  The
page reports what the tables show: a stage the program enters by a number it computes would be
listed too, and the page does not decide which of the listed rows the game really never enters.

## pal: SCES-50760 (the European disc)

The image holds 193 DATA.DF entries (68 packs), 106 stage rows and 261 exit rows.

### DATA.DF entries no table or program string names

| entry | bytes (compressed) | members | members a table names by path |
| --- | --- | --- | --- |
| ICON.DF | 51919 | 4 | 0 |
| OTHERS1.DF | 7487547 | 10 | 10 |
| STGMEMORY.DF | 36699 | 6 | 1 |

Named by a string in the program, not by a table: COMMON.DF, MOTION1.DF, MOTION2.DF.

Names listed twice in the directory: 24_2.int.

### Stage rows

65 of the 105 stage rows have their pack or movie on the disc; 60 of those are reached (a root below, or an exit row of a reached stage).

Roots: stage 1, boot: the logo (kanbanBoot.c); stage 57, the title's attract (op.c, stgmgrForceSwitchWithFade 57); stage 58, the title's attract in the 50 Hz mode (op.c, systemStatus[0] != 0 ? 58 : 57).

Stage rows a script names by a number (stgmgrForceSwitch*, RequestStageChangeSimple, RequestStageChangeDirect; the numbers are the source's own, every alternative of a region selector counted), first call site each:

| row | name | call site |
| --- | --- | --- |
| 1 | logo | common/kanban.c:654 |
| 11 | st04a (GATE_1ST) | script/st47a.c:2608 |
| 19 | st04c (CREST_L1) | script/st04b.c:625 |
| 22 | st02a (WATERFALL) | script/st06a.c:2980 |
| 28 | st05c (CREST_R1) | script/st05b.c:464 |

Reached by nothing, pack on the disc:

| row | name | file | what leads to it |
| --- | --- | --- | --- |
| 2 | title | STGTTL.DF | entrance row 220 names it; no stage lists that row as an exit |
| 38 | st26a (SEA) | STGST26A.DF | entrance row 14 names it; no stage lists that row as an exit |
| 59 | memoryPAL576 | pal_advertise576.pss | no exit row |
| 88 | boss_test | STGBOSS_TEST.DF | no exit row |
| 91 | 8a_TEST | STG8TEST.DF | entrance row 260 names it; no stage lists that row as an exit |

Rows whose pack or movie the disc does not hold: 28 development (64-87, 89-90, 92-93), 9 early show (94-102), 3 movie (103, 104, 105).

Movie rows:

| movie | file | on the disc | stage rows playing it | reached |
| --- | --- | --- | --- | --- |
| 1 | advertise.pss | yes | 57 | yes |
| 2 | pal_advertise.pss | yes | 58 | yes |
| 3 | pal_advertise576.pss | yes | 59 | no |
| 4 | pal_demo_advertise.pss | no | 103 | no |
| 5 | pal_end.pss | no | 104 | no |
| 6 | pal_badend.pss | no | 105 | no |

### Models in the logo pack and the test-room packs

A model is used by reachable content when the layout rows of a reached stage or a reached stage's model list hold its id, or an animation file in a pack names it in a node (the animation table's object ranges alone do not decide it: the retail title's range also holds the early show's art).

| pack | on the disc (bytes) | models | reachable content uses | only unreached stages use | no row names it |
| --- | --- | --- | --- | --- | --- |
| STGLOG.DF | 4001634 | 65 | 57 | 1 | 7 |
| STGBOSS_TEST.DF | 4146492 | 57 | 50 | 1 | 6 |
| STG8TEST.DF | 4908028 | 58 | 44 | 5 | 9 |

`No row names it` is every model no layout row, model list or animation names; the weapons and effects the scripts create by id are among them, so it is a list to read, not a verdict.  The early show's art has a folder of its own, listed after each pack.

No row names, in STGLOG.DF: bmd-buki.p2o (id 64), copy_right.p2o (id 1595), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), boy_sekika_aura2.p2o (id 1343), film_frame.p2o (id 1615).

Only unreached stages use, in STGLOG.DF: bmd-buki_ng.p2o (id 76, stage rows 64, 78).

The early show's title art, in STGLOG.DF (the folder object/sdf/e3):

| member | model id | bytes | used by |
| --- | --- | --- | --- |
| scei_cr.tm2 | - | 131200 | texture |
| copy_right.p2o | 1595 | 864 | no row names it |

No row names, in STGBOSS_TEST.DF: bmd-buki.p2o (id 64), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), boy_sekika_aura2.p2o (id 1343), film_frame.p2o (id 1615).

Only unreached stages use, in STGBOSS_TEST.DF: bmd-buki_ng.p2o (id 76, stage rows 64, 78).

No row names, in STG8TEST.DF: bmd-buki.p2o (id 64), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), switch2.p2o (id 154), boy_sekika_aura2.p2o (id 1343), film_frame.p2o (id 1615), switch_sp.p2o (id 630), truck_wheel.p2o (id 632).

Only unreached stages use, in STG8TEST.DF: st08a_p1.p2o (id 1140, stage rows 91); st08a_p2.p2o (id 1141, stage rows 91); st08a_p3.p2o (id 1142, stage rows 91); st08a_p4.p2o (id 1143, stage rows 91); bmd-buki_ng.p2o (id 76, stage rows 64, 78).

Scene-animation rows whose animation file is in no pack on this disc name 236 model ids.

### Loose items

| stream motion | file | on the disc | read by |
| --- | --- | --- | --- |
| 1 | d6.smb | yes | script/src/st04a.c |
| 2 | d9.smb | yes | script/src/st04a.c |
| 3 | d9a.smb | yes | script/src/st04a.c |
| 4 | d9b.smb | yes | script/src/st04a.c |
| 5 | d12.smb | yes | nothing |
| 6 | d12x.smb | yes | script/src/st25a.c |
| 7 | d12y.smb | yes | script/src/st25a.c |
| 8 | d13.smb | yes | script/src/st25a.c |
| 9 | e3d6.smb | yes | script/src/e3.c |

Sound streams the table names that are not on the disc: 4 (2 e3_1.int, 3 e3_2.int, 4 e3_3.int, 5 e3_4.int).

Subtitle file `Tex_jimaku/data.jim` (opened by ico2/fumi/src/jimaku.c on the discs that carry it): not named by the program, not present as a DATA.DF entry.

Debug files beside the game: MAIN.MAP (582413 bytes), SRCFILE.TXT (23982950 bytes), TRFILE.TXT (8503767 bytes), TRTABLE.BIN (892120 bytes), DUMMY.TXT (7 bytes).

## jpn: SCPS-11003 (the Japanese disc)

The image holds 183 DATA.DF entries (68 packs), 104 stage rows and 261 exit rows.

### DATA.DF entries no table or program string names

| entry | bytes (compressed) | members | members a table names by path |
| --- | --- | --- | --- |
| ICON.DF | 51919 | 4 | 0 |
| OTHERS1.DF | 1638752 | 2 | 2 |
| STGMEMORY.DF | 36699 | 6 | 1 |

Named by a string in the program, not by a table: COMMON.DF, MOTION1.DF, MOTION2.DF.

Names listed twice in the directory: 24_2.int.

### Stage rows

63 of the 103 stage rows have their pack or movie on the disc; 59 of those are reached (a root below, or an exit row of a reached stage).

Roots: stage 1, boot: the logo (kanbanBoot.c); stage 57, the title's attract (op.c, stgmgrForceSwitchWithFade 57).

Stage rows a script names by a number (stgmgrForceSwitch*, RequestStageChangeSimple, RequestStageChangeDirect; the numbers are the source's own, every alternative of a region selector counted), first call site each:

| row | name | call site |
| --- | --- | --- |
| 1 | logo | common/kanban.c:654 |
| 11 | st04a (GATE_1ST) | script/st47a.c:2608 |
| 19 | st04c (CREST_L1) | script/st04b.c:625 |
| 22 | st02a (WATERFALL) | script/st06a.c:2980 |
| 28 | st05c (CREST_R1) | script/st05b.c:464 |

Reached by nothing, pack on the disc:

| row | name | file | what leads to it |
| --- | --- | --- | --- |
| 2 | title | STGTTL.DF | entrance row 220 names it; no stage lists that row as an exit |
| 38 | st26a (SEA) | STGST26A.DF | entrance row 14 names it; no stage lists that row as an exit |
| 86 | boss_test | STGBOSS_TEST.DF | no exit row |
| 89 | 8a_TEST | STG8TEST.DF | entrance row 260 names it; no stage lists that row as an exit |

Rows whose pack or movie the disc does not hold: 28 development (62-85, 87-88, 90-91), 9 early show (92-100), 3 movie (101, 102, 103).

Movie rows:

| movie | file | on the disc | stage rows playing it | reached |
| --- | --- | --- | --- | --- |
| 1 | advertise.pss | yes | 57 | yes |
| 2 | e3_advertise.pss | no | 101 | no |
| 3 | e3_end.pss | no | 102 | no |
| 4 | e3_badend.pss | no | 103 | no |

### Models in the logo pack and the test-room packs

A model is used by reachable content when the layout rows of a reached stage or a reached stage's model list hold its id, or an animation file in a pack names it in a node (the animation table's object ranges alone do not decide it: the retail title's range also holds the early show's art).

| pack | on the disc (bytes) | models | reachable content uses | only unreached stages use | no row names it |
| --- | --- | --- | --- | --- | --- |
| STGLOG.DF | 3763677 | 67 | 57 | 1 | 9 |
| STGBOSS_TEST.DF | 3903072 | 57 | 50 | 1 | 6 |
| STG8TEST.DF | 4667197 | 58 | 44 | 5 | 9 |

`No row names it` is every model no layout row, model list or animation names; the weapons and effects the scripts create by id are among them, so it is a list to read, not a verdict.  The early show's art has a folder of its own, listed after each pack.

No row names, in STGLOG.DF: bmd-buki.p2o (id 64), copy_right.p2o (id 1579), e3_ver.p2o (id 1577), p_start.p2o (id 1578), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), boy_sekika_aura2.p2o (id 1339), film_frame.p2o (id 1599).

Only unreached stages use, in STGLOG.DF: bmd-buki_ng.p2o (id 76, stage rows 62, 76).

The early show's title art, in STGLOG.DF (the folder object/sdf/e3):

| member | model id | bytes | used by |
| --- | --- | --- | --- |
| e3_ver.tm2 | - | 32896 | texture |
| p_start.tm2 | - | 32896 | texture |
| scei_cr.tm2 | - | 131200 | texture |
| copy_right.p2o | 1579 | 864 | no row names it |
| e3_ver.p2o | 1577 | 864 | no row names it |
| p_start.p2o | 1578 | 864 | no row names it |

No row names, in STGBOSS_TEST.DF: bmd-buki.p2o (id 64), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), boy_sekika_aura2.p2o (id 1339), film_frame.p2o (id 1599).

Only unreached stages use, in STGBOSS_TEST.DF: bmd-buki_ng.p2o (id 76, stage rows 62, 76).

No row names, in STG8TEST.DF: bmd-buki.p2o (id 64), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), switch2.p2o (id 154), boy_sekika_aura2.p2o (id 1339), film_frame.p2o (id 1599), switch_sp.p2o (id 630), truck_wheel.p2o (id 632).

Only unreached stages use, in STG8TEST.DF: st08a_p1.p2o (id 1136, stage rows 89); st08a_p2.p2o (id 1137, stage rows 89); st08a_p3.p2o (id 1138, stage rows 89); st08a_p4.p2o (id 1139, stage rows 89); bmd-buki_ng.p2o (id 76, stage rows 62, 76).

Scene-animation rows whose animation file is in no pack on this disc name 224 model ids.

### Loose items

| stream motion | file | on the disc | read by |
| --- | --- | --- | --- |
| 1 | d6.smb | yes | script/src/st04a.c |
| 2 | d9.smb | yes | script/src/st04a.c |
| 3 | d9a.smb | yes | script/src/st04a.c |
| 4 | d9b.smb | yes | script/src/st04a.c |
| 5 | d12.smb | yes | nothing |
| 6 | d12x.smb | yes | script/src/st25a.c |
| 7 | d12y.smb | yes | script/src/st25a.c |
| 8 | d13.smb | yes | script/src/st25a.c |
| 9 | e3d6.smb | yes | script/src/e3.c |

Sound streams the table names that are not on the disc: 4 (2 e3_1.int, 3 e3_2.int, 4 e3_3.int, 5 e3_4.int).

Subtitle file `Tex_jimaku/data.jim` (opened by ico2/fumi/src/jimaku.c on the discs that carry it): named by the program, present as a DATA.DF entry.

Debug files beside the game: none.

## kor: SCPS-56001 (the Korean disc)

The image holds 183 DATA.DF entries (68 packs), 104 stage rows and 261 exit rows.

### DATA.DF entries no table or program string names

| entry | bytes (compressed) | members | members a table names by path |
| --- | --- | --- | --- |
| ICON.DF | 51919 | 4 | 0 |
| OTHERS1.DF | 1219845 | 2 | 2 |
| STGMEMORY.DF | 36699 | 6 | 1 |

Named by a string in the program, not by a table: COMMON.DF, MOTION1.DF, MOTION2.DF.

Names listed twice in the directory: 24_2.int.

### Stage rows

63 of the 103 stage rows have their pack or movie on the disc; 59 of those are reached (a root below, or an exit row of a reached stage).

Roots: stage 1, boot: the logo (kanbanBoot.c); stage 57, the title's attract (op.c, stgmgrForceSwitchWithFade 57).

Stage rows a script names by a number (stgmgrForceSwitch*, RequestStageChangeSimple, RequestStageChangeDirect; the numbers are the source's own, every alternative of a region selector counted), first call site each:

| row | name | call site |
| --- | --- | --- |
| 1 | logo | common/kanban.c:654 |
| 11 | st04a (GATE_1ST) | script/st47a.c:2608 |
| 19 | st04c (CREST_L1) | script/st04b.c:625 |
| 22 | st02a (WATERFALL) | script/st06a.c:2980 |
| 28 | st05c (CREST_R1) | script/st05b.c:464 |

Reached by nothing, pack on the disc:

| row | name | file | what leads to it |
| --- | --- | --- | --- |
| 2 | title | STGTTL.DF | entrance row 220 names it; no stage lists that row as an exit |
| 38 | st26a (SEA) | STGST26A.DF | entrance row 14 names it; no stage lists that row as an exit |
| 86 | boss_test | STGBOSS_TEST.DF | no exit row |
| 89 | 8a_TEST | STG8TEST.DF | entrance row 260 names it; no stage lists that row as an exit |

Rows whose pack or movie the disc does not hold: 28 development (62-85, 87-88, 90-91), 9 early show (92-100), 3 movie (101, 102, 103).

Movie rows:

| movie | file | on the disc | stage rows playing it | reached |
| --- | --- | --- | --- | --- |
| 1 | advertise.pss | yes | 57 | yes |
| 2 | e3_advertise.pss | no | 101 | no |
| 3 | e3_end.pss | no | 102 | no |
| 4 | e3_badend.pss | no | 103 | no |

### Models in the logo pack and the test-room packs

A model is used by reachable content when the layout rows of a reached stage or a reached stage's model list hold its id, or an animation file in a pack names it in a node (the animation table's object ranges alone do not decide it: the retail title's range also holds the early show's art).

| pack | on the disc (bytes) | models | reachable content uses | only unreached stages use | no row names it |
| --- | --- | --- | --- | --- | --- |
| STGLOG.DF | 3750373 | 67 | 57 | 1 | 9 |
| STGBOSS_TEST.DF | 3893041 | 57 | 50 | 1 | 6 |
| STG8TEST.DF | 4655288 | 58 | 44 | 5 | 9 |

`No row names it` is every model no layout row, model list or animation names; the weapons and effects the scripts create by id are among them, so it is a list to read, not a verdict.  The early show's art has a folder of its own, listed after each pack.

No row names, in STGLOG.DF: bmd-buki.p2o (id 64), copy_right.p2o (id 1579), e3_ver.p2o (id 1577), p_start.p2o (id 1578), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), boy_sekika_aura2.p2o (id 1339), film_frame.p2o (id 1599).

Only unreached stages use, in STGLOG.DF: bmd-buki_ng.p2o (id 76, stage rows 62, 76).

The early show's title art, in STGLOG.DF (the folder object/sdf/e3):

| member | model id | bytes | used by |
| --- | --- | --- | --- |
| e3_ver.tm2 | - | 32896 | texture |
| p_start.tm2 | - | 32896 | texture |
| scei_cr.tm2 | - | 131200 | texture |
| copy_right.p2o | 1579 | 864 | no row names it |
| e3_ver.p2o | 1577 | 864 | no row names it |
| p_start.p2o | 1578 | 864 | no row names it |

No row names, in STGBOSS_TEST.DF: bmd-buki.p2o (id 64), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), boy_sekika_aura2.p2o (id 1339), film_frame.p2o (id 1599).

Only unreached stages use, in STGBOSS_TEST.DF: bmd-buki_ng.p2o (id 76, stage rows 62, 76).

No row names, in STG8TEST.DF: bmd-buki.p2o (id 64), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), switch2.p2o (id 154), boy_sekika_aura2.p2o (id 1339), film_frame.p2o (id 1599), switch_sp.p2o (id 630), truck_wheel.p2o (id 632).

Only unreached stages use, in STG8TEST.DF: st08a_p1.p2o (id 1136, stage rows 89); st08a_p2.p2o (id 1137, stage rows 89); st08a_p3.p2o (id 1138, stage rows 89); st08a_p4.p2o (id 1139, stage rows 89); bmd-buki_ng.p2o (id 76, stage rows 62, 76).

Scene-animation rows whose animation file is in no pack on this disc name 224 model ids.

### Loose items

| stream motion | file | on the disc | read by |
| --- | --- | --- | --- |
| 1 | d6.smb | yes | script/src/st04a.c |
| 2 | d9.smb | yes | script/src/st04a.c |
| 3 | d9a.smb | yes | script/src/st04a.c |
| 4 | d9b.smb | yes | script/src/st04a.c |
| 5 | d12.smb | yes | nothing |
| 6 | d12x.smb | yes | script/src/st25a.c |
| 7 | d12y.smb | yes | script/src/st25a.c |
| 8 | d13.smb | yes | script/src/st25a.c |
| 9 | e3d6.smb | yes | script/src/e3.c |

Sound streams the table names that are not on the disc: 4 (2 e3_1.int, 3 e3_2.int, 4 e3_3.int, 5 e3_4.int).

Subtitle file `Tex_jimaku/data.jim` (opened by ico2/fumi/src/jimaku.c on the discs that carry it): named by the program, present as a DATA.DF entry.

Debug files beside the game: none.

## ntsc: SCUS-97113 (the North American disc)

The image holds 172 DATA.DF entries (68 packs), 104 stage rows and 257 exit rows.

### DATA.DF entries no table or program string names

| entry | bytes (compressed) | members | members a table names by path |
| --- | --- | --- | --- |
| ICON.DF | 51922 | 4 | 0 |
| OTHERS1.DF | 577873 | 1 | 0 |
| STGMEMORY.DF | 36699 | 6 | 2 |

Named by a string in the program, not by a table: COMMON.DF, MOTION1.DF, MOTION2.DF, data.jim.

### Stage rows

64 of the 103 stage rows have their pack or movie on the disc; 60 of those are reached (a root below, or an exit row of a reached stage).

Roots: stage 1, boot: the logo (kanbanBoot.c); stage 57, the title's attract (op.c, stgmgrForceSwitchWithFade 57).

Stage rows a script names by a number (stgmgrForceSwitch*, RequestStageChangeSimple, RequestStageChangeDirect; the numbers are the source's own, every alternative of a region selector counted), first call site each:

| row | name | call site |
| --- | --- | --- |
| 1 | logo | common/kanban.c:654 |
| 11 | st04a (GATE_1ST) | script/st47a.c:2608 |
| 19 | st04c (CREST_L1) | script/st04b.c:625 |
| 22 | st02a (WATERFALL) | script/st06a.c:2980 |
| 28 | st05c (CREST_R1) | script/st05b.c:464 |
| 101 | E3_ADVERTISE1 | common/main.c:885 |

Reached by nothing, pack on the disc:

| row | name | file | what leads to it |
| --- | --- | --- | --- |
| 2 | title | STGTTL.DF | entrance row 216 names it; no stage lists that row as an exit |
| 38 | st26a (SEA) | STGST26A.DF | entrance row 14 names it; no stage lists that row as an exit |
| 86 | boss_test | STGBOSS_TEST.DF | no exit row |
| 89 | 8a_TEST | STG8TEST.DF | entrance row 256 names it; no stage lists that row as an exit |

Rows whose pack or movie the disc does not hold: 28 development (62-85, 87-88, 90-91), 9 early show (92-100), 2 movie (102, 103).

Movie rows:

| movie | file | on the disc | stage rows playing it | reached |
| --- | --- | --- | --- | --- |
| 1 | advertise.pss | yes | 57 | yes |
| 2 | advertise.pss | yes | 101 | yes |
| 3 | e3_end.pss | no | 102 | no |
| 4 | e3_badend.pss | no | 103 | no |

### Models in the logo pack and the test-room packs

A model is used by reachable content when the layout rows of a reached stage or a reached stage's model list hold its id, or an animation file in a pack names it in a node (the animation table's object ranges alone do not decide it: the retail title's range also holds the early show's art).

| pack | on the disc (bytes) | models | reachable content uses | only unreached stages use | no row names it |
| --- | --- | --- | --- | --- | --- |
| STGLOG.DF | 3670408 | 57 | 46 | 2 | 9 |
| STGBOSS_TEST.DF | 3580067 | 48 | 40 | 2 | 6 |
| STG8TEST.DF | 4615498 | 49 | 34 | 6 | 9 |

`No row names it` is every model no layout row, model list or animation names; the weapons and effects the scripts create by id are among them, so it is a list to read, not a verdict.  The early show's art has a folder of its own, listed after each pack.

No row names, in STGLOG.DF: bmd-buki.p2o (id 59), copy_right.p2o (id 1546), e3_ver.p2o (id 1544), p_start.p2o (id 1545), hair_tail.p2o (id 9), hair_tail_last.p2o (id 10), box_water.p2o (id 58), long_sord_cloth.p2o (id 75), boy_sekika_aura2.p2o (id 1307).

Only unreached stages use, in STGLOG.DF: bmd-buki_ng.p2o (id 70, stage rows 62, 76); pot.p2o (id 92, stage rows 63, 98).

The early show's title art, in STGLOG.DF (the folder object/sdf/e3):

| member | model id | bytes | used by |
| --- | --- | --- | --- |
| e3_ver.tm2 | - | 32896 | texture |
| p_start.tm2 | - | 32896 | texture |
| scei_cr.tm2 | - | 131200 | texture |
| copy_right.p2o | 1546 | 864 | no row names it |
| e3_ver.p2o | 1544 | 864 | no row names it |
| p_start.p2o | 1545 | 864 | no row names it |

No row names, in STGBOSS_TEST.DF: bmd-buki.p2o (id 59), hair_tail.p2o (id 9), hair_tail_last.p2o (id 10), box_water.p2o (id 58), long_sord_cloth.p2o (id 75), boy_sekika_aura2.p2o (id 1307).

Only unreached stages use, in STGBOSS_TEST.DF: bmd-buki_ng.p2o (id 70, stage rows 62, 76); pot.p2o (id 92, stage rows 63, 98).

No row names, in STG8TEST.DF: bmd-buki.p2o (id 59), hair_tail.p2o (id 9), hair_tail_last.p2o (id 10), box_water.p2o (id 58), long_sord_cloth.p2o (id 75), switch2.p2o (id 145), boy_sekika_aura2.p2o (id 1307), switch_sp.p2o (id 615), truck_wheel.p2o (id 617).

Only unreached stages use, in STG8TEST.DF: st08a_p1.p2o (id 1105, stage rows 89); st08a_p2.p2o (id 1106, stage rows 89); st08a_p3.p2o (id 1107, stage rows 89); st08a_p4.p2o (id 1108, stage rows 89); bmd-buki_ng.p2o (id 70, stage rows 62, 76); pot.p2o (id 92, stage rows 63, 98).

Scene-animation rows whose animation file is in no pack on this disc name 242 model ids.

### Loose items

| stream motion | file | on the disc | read by |
| --- | --- | --- | --- |
| 1 | d6.smb | yes | script/src/st04a.c |
| 2 | d9.smb | yes | script/src/st04a.c |
| 3 | d9a.smb | yes | script/src/st04a.c |
| 4 | d9b.smb | yes | script/src/st04a.c |
| 5 | d12.smb | yes | nothing |
| 6 | d12x.smb | yes | script/src/st25a.c |
| 7 | d12y.smb | yes | script/src/st25a.c |
| 8 | d13.smb | yes | script/src/st25a.c |
| 9 | e3d6.smb | yes | script/src/e3.c |

Sound streams the table names that are not on the disc: 4 (2 e3_1.int, 3 e3_2.int, 4 e3_3.int, 5 e3_4.int).

Subtitle file `Tex_jimaku/data.jim` (opened by ico2/fumi/src/jimaku.c on the discs that carry it): named by the program, present as a DATA.DF entry.

Debug files beside the game: none.

Other files in the game's folder: LDUMMY (10485760 bytes).

## aug6: PROTO-20010806 (the August 2001 preview disc)

The image holds 172 DATA.DF entries (68 packs), 104 stage rows and 257 exit rows.

### DATA.DF entries no table or program string names

| entry | bytes (compressed) | members | members a table names by path |
| --- | --- | --- | --- |
| ICON.DF | 51921 | 4 | 0 |
| OTHERS1.DF | 577873 | 1 | 0 |
| STGMEMORY.DF | 36699 | 6 | 2 |

Named by a string in the program, not by a table: COMMON.DF, MOTION1.DF, MOTION2.DF, data.jim.

### Stage rows

64 of the 103 stage rows have their pack or movie on the disc; 60 of those are reached (a root below, or an exit row of a reached stage).

Roots: stage 1, boot: the logo (kanbanBoot.c); stage 57, the title's attract (op.c, stgmgrForceSwitchWithFade 57).

Stage rows a script names by a number (stgmgrForceSwitch*, RequestStageChangeSimple, RequestStageChangeDirect; the numbers are the source's own, every alternative of a region selector counted), first call site each:

| row | name | call site |
| --- | --- | --- |
| 1 | logo | common/kanban.c:654 |
| 11 | st04a (GATE_1ST) | script/st47a.c:2608 |
| 19 | st04c (CREST_L1) | script/st04b.c:625 |
| 22 | st02a (WATERFALL) | script/st06a.c:2980 |
| 28 | st05c (CREST_R1) | script/st05b.c:464 |
| 101 | E3_ADVERTISE1 | common/main.c:885 |

Reached by nothing, pack on the disc:

| row | name | file | what leads to it |
| --- | --- | --- | --- |
| 2 | title | STGTTL.DF | entrance row 216 names it; no stage lists that row as an exit |
| 38 | st26a (SEA) | STGST26A.DF | entrance row 14 names it; no stage lists that row as an exit |
| 86 | boss_test | STGBOSS_TEST.DF | no exit row |
| 89 | 8a_TEST | STG8TEST.DF | entrance row 256 names it; no stage lists that row as an exit |

Rows whose pack or movie the disc does not hold: 28 development (62-85, 87-88, 90-91), 9 early show (92-100), 2 movie (102, 103).

Movie rows:

| movie | file | on the disc | stage rows playing it | reached |
| --- | --- | --- | --- | --- |
| 1 | advertise.pss | yes | 57 | yes |
| 2 | advertise.pss | yes | 101 | yes |
| 3 | e3_end.pss | no | 102 | no |
| 4 | e3_badend.pss | no | 103 | no |

### Models in the logo pack and the test-room packs

A model is used by reachable content when the layout rows of a reached stage or a reached stage's model list hold its id, or an animation file in a pack names it in a node (the animation table's object ranges alone do not decide it: the retail title's range also holds the early show's art).

| pack | on the disc (bytes) | models | reachable content uses | only unreached stages use | no row names it |
| --- | --- | --- | --- | --- | --- |
| STGLOG.DF | 3678670 | 56 | 46 | 2 | 8 |
| STGBOSS_TEST.DF | 3579926 | 47 | 40 | 2 | 5 |
| STG8TEST.DF | 4615322 | 48 | 34 | 6 | 8 |

`No row names it` is every model no layout row, model list or animation names; the weapons and effects the scripts create by id are among them, so it is a list to read, not a verdict.  The early show's art has a folder of its own, listed after each pack.

No row names, in STGLOG.DF: copy_right.p2o (id 1541), e3_ver.p2o (id 1539), p_start.p2o (id 1540), hair_tail.p2o (id 9), hair_tail_last.p2o (id 10), box_water.p2o (id 58), long_sord_cloth.p2o (id 74), boy_sekika_aura2.p2o (id 1302).

Only unreached stages use, in STGLOG.DF: bmd-buki_ng.p2o (id 69, stage rows 62, 76); pot.p2o (id 91, stage rows 63, 98).

The early show's title art, in STGLOG.DF (the folder object/sdf/e3):

| member | model id | bytes | used by |
| --- | --- | --- | --- |
| e3_ver.tm2 | - | 32896 | texture |
| p_start.tm2 | - | 32896 | texture |
| scei_cr.tm2 | - | 131200 | texture |
| copy_right.p2o | 1541 | 864 | no row names it |
| e3_ver.p2o | 1539 | 864 | no row names it |
| p_start.p2o | 1540 | 864 | no row names it |

No row names, in STGBOSS_TEST.DF: hair_tail.p2o (id 9), hair_tail_last.p2o (id 10), box_water.p2o (id 58), long_sord_cloth.p2o (id 74), boy_sekika_aura2.p2o (id 1302).

Only unreached stages use, in STGBOSS_TEST.DF: bmd-buki_ng.p2o (id 69, stage rows 62, 76); pot.p2o (id 91, stage rows 63, 98).

No row names, in STG8TEST.DF: hair_tail.p2o (id 9), hair_tail_last.p2o (id 10), box_water.p2o (id 58), long_sord_cloth.p2o (id 74), switch2.p2o (id 144), boy_sekika_aura2.p2o (id 1302), switch_sp.p2o (id 610), truck_wheel.p2o (id 612).

Only unreached stages use, in STG8TEST.DF: st08a_p1.p2o (id 1100, stage rows 89); st08a_p2.p2o (id 1101, stage rows 89); st08a_p3.p2o (id 1102, stage rows 89); st08a_p4.p2o (id 1103, stage rows 89); bmd-buki_ng.p2o (id 69, stage rows 62, 76); pot.p2o (id 91, stage rows 63, 98).

Scene-animation rows whose animation file is in no pack on this disc name 242 model ids.

### Loose items

| stream motion | file | on the disc | read by |
| --- | --- | --- | --- |
| 1 | d6.smb | yes | script/src/st04a.c |
| 2 | d9.smb | yes | script/src/st04a.c |
| 3 | d9a.smb | yes | script/src/st04a.c |
| 4 | d9b.smb | yes | script/src/st04a.c |
| 5 | d12.smb | yes | nothing |
| 6 | d12x.smb | yes | script/src/st25a.c |
| 7 | d12y.smb | yes | script/src/st25a.c |
| 8 | d13.smb | yes | script/src/st25a.c |
| 9 | e3d6.smb | yes | script/src/e3.c |

Sound streams the table names that are not on the disc: 4 (2 e3_1.int, 3 e3_2.int, 4 e3_3.int, 5 e3_4.int).

Subtitle file `Tex_jimaku/data.jim` (opened by ico2/fumi/src/jimaku.c on the discs that carry it): named by the program, present as a DATA.DF entry.

Debug files beside the game: MAIN.MAP (582413 bytes), TRFILE.TXT (7869588 bytes), TRTABLE.BIN (826064 bytes), DUMMY.TXT (7 bytes).

## jun26: SCUS-97158 (the June 2001 magazine demo)

The image holds 49 DATA.DF entries (17 packs), 103 stage rows and 256 exit rows.

### DATA.DF entries no table or program string names

| entry | bytes (compressed) | members | members a table names by path |
| --- | --- | --- | --- |
| ICON.DF | 51922 | 4 | 0 |
| OTHERS1.DF | 804341 | 1 | 0 |
| STGONLYSAMPLE_E3AD1.DF | 36369 | 5 | 2 |
| STGONLYSAMPLE_E3BADEND.DF | 36368 | 5 | 2 |
| STGONLYSAMPLE_E3END.DF | 36369 | 5 | 2 |

Named by a string in the program, not by a table: COMMON.DF, MOTION1.DF, MOTION2.DF, data.jim.

### Stage rows

13 of the 102 stage rows have their pack or movie on the disc, and the boot stage (row 1, STGLOG.DF) is not one of them: the game's tables are whole but the disc's packs are the early show's samples only, so no verdict on reached rows is made.  The rows with a pack:

| row | name | file | what leads to it |
| --- | --- | --- | --- |
| 58 | memory | advertise.pss | - |
| 91 | E3_WARNING | STGONLYSAMPLE_WARNING.DF | - |
| 92 | E3_TITLE | STGONLYSAMPLE_E3_TITLE.DF | - |
| 93 | E3_INST1 | STGONLYSAMPLE_INST1.DF | - |
| 94 | E3_st13b | STGONLYSAMPLE_ST13B.DF | - |
| 95 | E3_st13d | STGONLYSAMPLE_ST13D.DF | - |
| 96 | E3_st13c | STGONLYSAMPLE_ST13C.DF | - |
| 97 | E3_st01b | STGONLYSAMPLE_ST01B.DF | - |
| 98 | E3_st09a | STGONLYSAMPLE_ST09A.DF | - |
| 99 | E3_st04a | STGONLYSAMPLE_ST04A.DF | - |
| 100 | E3_ADVERTISE1 | advertise.pss | - |
| 101 | E3_END_MOVIE | e3_end.pss | - |
| 102 | E3_BADEND_MOVIE | e3_badend.pss | - |

Rows whose pack or movie the disc does not hold: 25 development (64-84, 86-87, 89-90), 64 stage (1-57, 59-63, 85, 88).

Movie rows:

| movie | file | on the disc | stage rows playing it | reached |
| --- | --- | --- | --- | --- |
| 1 | advertise.pss | yes | 58 | no |
| 2 | advertise.pss | yes | 100 | no |
| 3 | e3_end.pss | yes | 101 | no |
| 4 | e3_badend.pss | yes | 102 | no |

### Models in the logo pack and the test-room packs

A model is used by reachable content when the layout rows of a reached stage or a reached stage's model list hold its id, or an animation file in a pack names it in a node (the animation table's object ranges alone do not decide it: the retail title's range also holds the early show's art).

| pack | on the disc (bytes) | models | reachable content uses | only unreached stages use | no row names it |
| --- | --- | --- | --- | --- | --- |
| STGLOG.DF | no | | | | |
| STGBOSS_TEST.DF | no | | | | |
| STG8TEST.DF | no | | | | |

`No row names it` is every model no layout row, model list or animation names; the weapons and effects the scripts create by id are among them, so it is a list to read, not a verdict.  The early show's art has a folder of its own, listed after each pack.

Scene-animation rows whose animation file is in no pack on this disc name 777 model ids.

### Loose items

| stream motion | file | on the disc | read by |
| --- | --- | --- | --- |
| 1 | d6.smb | yes | script/src/st04a.c |
| 2 | d9.smb | yes | script/src/st04a.c |
| 3 | d9a.smb | yes | script/src/st04a.c |
| 4 | d9b.smb | yes | script/src/st04a.c |
| 5 | d12.smb | yes | nothing |
| 6 | d12x.smb | yes | script/src/st25a.c |
| 7 | d12y.smb | yes | script/src/st25a.c |
| 8 | d13.smb | yes | script/src/st25a.c |
| 9 | e3d6.smb | yes | script/src/e3.c |

Sound streams the table names that are not on the disc: 72 (2 m08.int, 3 m09.int, 9 01.int, 10 02_1a.int, 11 02_1b.int, 12 02_1c.int, 13 02_1d.int, 14 02_2.int, and more).

Subtitle file `Tex_jimaku/data.jim` (opened by ico2/fumi/src/jimaku.c on the discs that carry it): named by the program, present as a DATA.DF entry.

Debug files beside the game: none.

## cn: SCPS-55001 (the Asian disc)

The image holds 183 DATA.DF entries (68 packs), 104 stage rows and 261 exit rows.

### DATA.DF entries no table or program string names

| entry | bytes (compressed) | members | members a table names by path |
| --- | --- | --- | --- |
| ICON.DF | 51919 | 4 | 0 |
| OTHERS1.DF | 1156981 | 2 | 2 |
| STGMEMORY.DF | 36699 | 6 | 1 |

Named by a string in the program, not by a table: COMMON.DF, MOTION1.DF, MOTION2.DF.

Names listed twice in the directory: 24_2.int.

### Stage rows

63 of the 103 stage rows have their pack or movie on the disc; 59 of those are reached (a root below, or an exit row of a reached stage).

Roots: stage 1, boot: the logo (kanbanBoot.c); stage 57, the title's attract (op.c, stgmgrForceSwitchWithFade 57).

Stage rows a script names by a number (stgmgrForceSwitch*, RequestStageChangeSimple, RequestStageChangeDirect; the numbers are the source's own, every alternative of a region selector counted), first call site each:

| row | name | call site |
| --- | --- | --- |
| 1 | logo | common/kanban.c:654 |
| 11 | st04a (GATE_1ST) | script/st47a.c:2608 |
| 19 | st04c (CREST_L1) | script/st04b.c:625 |
| 22 | st02a (WATERFALL) | script/st06a.c:2980 |
| 28 | st05c (CREST_R1) | script/st05b.c:464 |

Reached by nothing, pack on the disc:

| row | name | file | what leads to it |
| --- | --- | --- | --- |
| 2 | title | STGTTL.DF | entrance row 220 names it; no stage lists that row as an exit |
| 38 | st26a (SEA) | STGST26A.DF | entrance row 14 names it; no stage lists that row as an exit |
| 86 | boss_test | STGBOSS_TEST.DF | no exit row |
| 89 | 8a_TEST | STG8TEST.DF | entrance row 260 names it; no stage lists that row as an exit |

Rows whose pack or movie the disc does not hold: 28 development (62-85, 87-88, 90-91), 9 early show (92-100), 3 movie (101, 102, 103).

Movie rows:

| movie | file | on the disc | stage rows playing it | reached |
| --- | --- | --- | --- | --- |
| 1 | advertise.pss | yes | 57 | yes |
| 2 | e3_advertise.pss | no | 101 | no |
| 3 | e3_end.pss | no | 102 | no |
| 4 | e3_badend.pss | no | 103 | no |

### Models in the logo pack and the test-room packs

A model is used by reachable content when the layout rows of a reached stage or a reached stage's model list hold its id, or an animation file in a pack names it in a node (the animation table's object ranges alone do not decide it: the retail title's range also holds the early show's art).

| pack | on the disc (bytes) | models | reachable content uses | only unreached stages use | no row names it |
| --- | --- | --- | --- | --- | --- |
| STGLOG.DF | 3762085 | 67 | 57 | 1 | 9 |
| STGBOSS_TEST.DF | 3901368 | 57 | 50 | 1 | 6 |
| STG8TEST.DF | 4664566 | 58 | 44 | 5 | 9 |

`No row names it` is every model no layout row, model list or animation names; the weapons and effects the scripts create by id are among them, so it is a list to read, not a verdict.  The early show's art has a folder of its own, listed after each pack.

No row names, in STGLOG.DF: bmd-buki.p2o (id 64), copy_right.p2o (id 1579), e3_ver.p2o (id 1577), p_start.p2o (id 1578), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), boy_sekika_aura2.p2o (id 1339), film_frame.p2o (id 1599).

Only unreached stages use, in STGLOG.DF: bmd-buki_ng.p2o (id 76, stage rows 62, 76).

The early show's title art, in STGLOG.DF (the folder object/sdf/e3):

| member | model id | bytes | used by |
| --- | --- | --- | --- |
| e3_ver.tm2 | - | 32896 | texture |
| p_start.tm2 | - | 32896 | texture |
| scei_cr.tm2 | - | 131200 | texture |
| copy_right.p2o | 1579 | 864 | no row names it |
| e3_ver.p2o | 1577 | 864 | no row names it |
| p_start.p2o | 1578 | 864 | no row names it |

No row names, in STGBOSS_TEST.DF: bmd-buki.p2o (id 64), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), boy_sekika_aura2.p2o (id 1339), film_frame.p2o (id 1599).

Only unreached stages use, in STGBOSS_TEST.DF: bmd-buki_ng.p2o (id 76, stage rows 62, 76).

No row names, in STG8TEST.DF: bmd-buki.p2o (id 64), hair_tail.p2o (id 11), box_water.p2o (id 63), long_sord_cloth.p2o (id 81), switch2.p2o (id 154), boy_sekika_aura2.p2o (id 1339), film_frame.p2o (id 1599), switch_sp.p2o (id 630), truck_wheel.p2o (id 632).

Only unreached stages use, in STG8TEST.DF: st08a_p1.p2o (id 1136, stage rows 89); st08a_p2.p2o (id 1137, stage rows 89); st08a_p3.p2o (id 1138, stage rows 89); st08a_p4.p2o (id 1139, stage rows 89); bmd-buki_ng.p2o (id 76, stage rows 62, 76).

Scene-animation rows whose animation file is in no pack on this disc name 224 model ids.

### Loose items

| stream motion | file | on the disc | read by |
| --- | --- | --- | --- |
| 1 | d6.smb | yes | script/src/st04a.c |
| 2 | d9.smb | yes | script/src/st04a.c |
| 3 | d9a.smb | yes | script/src/st04a.c |
| 4 | d9b.smb | yes | script/src/st04a.c |
| 5 | d12.smb | yes | nothing |
| 6 | d12x.smb | yes | script/src/st25a.c |
| 7 | d12y.smb | yes | script/src/st25a.c |
| 8 | d13.smb | yes | script/src/st25a.c |
| 9 | e3d6.smb | yes | script/src/e3.c |

Sound streams the table names that are not on the disc: 4 (2 e3_1.int, 3 e3_2.int, 4 e3_3.int, 5 e3_4.int).

Subtitle file `Tex_jimaku/data.jim` (opened by ico2/fumi/src/jimaku.c on the discs that carry it): named by the program, present as a DATA.DF entry.

Debug files beside the game: none.
