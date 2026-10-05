#include "ee_view.h"
#include "debug.h"
#include "memory.h"
#include "pad.h"
#include "gobj.h"
#include "gobj_dl.h"
#include "gobj_process.h"
#include "act.h"
#include "camera-root.h"
#include "lineManager.h"
#include "matrixDrive.h"
#include "motionManager2.h"
#include <stdio.h>
#include <eekernel.h>
#include <libvu0.h>
#include <sifdev.h>
#include "way_llf.h"
#include "typedef.h"
#include "vobj.h"
#include "geometryManager.h"
#include "ios.h"
#include "GifPacket.h"
#include <string.h>
#include "way_util.h"
#include "way_tool.h"
#include "main.h"

/* way_tool.o .data +0x00: the scratch world position the tool builds a point
   at; the fourth word is the homogeneous 1.0f. */
static float wayWorkPos[4] = {0.0f, 0.0f, 0.0f, 1.0f}; /* derived name */

/* the way point the tool has picked, -1 for none: point_delete sets it,
   point_nige moves it and set_way_point_color highlights it */
static int wayPointSel = -1; /* derived name */

/* the way record the tool is showing, the group the selection window is on,
   the camera target saved while the tool holds the camera, and the cursor
   object */
static WayGroup *selectedWay; /* derived name */

static int wayGroupSel; /* derived name */

static GObj *savedCamTarget; /* derived name */

static GObj *cursorGObj; /* derived name */

typedef struct { /* field names derived */
    int c[4];
} WayCol; /* derived name */

/* the colour packet every way point is drawn through, the pad handle and its
   read buffer, the stick buffer, and the 32-byte way-point file block
   quick_save_wpfile writes and quick_load_wpfile reads back */
static WayCol wayDrawCol; /* derived name */

static IosPadCtx wayToolPad; /* derived name */

static char wayToolStick[32]; /* derived name */

static unsigned char wpBuf[32]; /* derived name */

/* the last 0x220 bytes of the TU's .bss, which nothing in the retail build
   reads */
static char wayToolBuf[544]; /* derived name */

inline void cursor_control(GObj *volatile self);

static int group_create(void)
{
    static int createState = 0; /* derived name */
    int f;

    if (debug_font_flag & 1) {
        debug_Printf(18, 54, 0xFF000000, "group + create");
    }
    if (createState == 0) {
        int g = CreateWayGroup();

        createState = 1;
        current_select_gid = g;
        selectedWay = &way_group[g];
        debug_StdPrintfDummy("search:%p %p\n", isysGObjSearchFromObjKindID_begin(0),
                             (void *)boyGObj);
        return 0;
    }
    if (createState != 1) {
        return 0;
    }
    if (debug_font_flag & 1) {
        debug_Printf(26, 66, 0xFF808000, "pt.%d", selectedWay->count);
    }
    f = wayToolPad.trg;
    if (f & 0x20) {
        int p = CreateWayPoint(wayWorkPos);

        AddWayPoint(current_select_gid, p);
        debug_StdPrintfDummy("create waypoint %d\n", p);
        return 0;
    }
    if (f & 0x40) {
        if (selectedWay->count == 0) {
            DeleteWayGroup(current_select_gid);
        }
        createState = 0;
        return -1;
    }
    if (f & 0x80) {
        CloseWayGroup(current_select_gid);
        createState = 0;
        return -1;
    }
    return 0;
}

/* one line of the way-group selector: debug_SelectCsvWindow walks debugWayGroupSelect
   with stride 8 and dereferences the first word */
typedef struct { /* field names derived */
    char *s;
    int _4;
} WayMenuLine; /* derived name */

/* the way-group selector's 64 lines, each label a string literal that
   relabel_way_groups rewrites in place */

WayMenuLine debugWayGroupSelect[64] = {
    {" 0 ( -)  ", 0}, {" 1 ( -)  ", 0}, {" 2 ( -)  ", 0}, {" 3 ( -)  ", 0}, {" 4 ( -)  ", 0},
    {" 5 ( -)  ", 0}, {" 6 ( -)  ", 0}, {" 7 ( -)  ", 0}, {" 8 ( -)  ", 0}, {" 9 ( -)  ", 0},
    {"10 ( -)  ", 0}, {"11 ( -)  ", 0}, {"12 ( -)  ", 0}, {"13 ( -)  ", 0}, {"14 ( -)  ", 0},
    {"15 ( -)  ", 0}, {"16 ( -)  ", 0}, {"17 ( -)  ", 0}, {"18 ( -)  ", 0}, {"19 ( -)  ", 0},
    {"20 ( -)  ", 0}, {"21 ( -)  ", 0}, {"22 ( -)  ", 0}, {"23 ( -)  ", 0}, {"24 ( -)  ", 0},
    {"25 ( -)  ", 0}, {"26 ( -)  ", 0}, {"27 ( -)  ", 0}, {"28 ( -)  ", 0}, {"29 ( -)  ", 0},
    {"30 ( -)  ", 0}, {"31 ( -)  ", 0}, {"32 ( -)  ", 0}, {"33 ( -)  ", 0}, {"34 ( -)  ", 0},
    {"35 ( -)  ", 0}, {"36 ( -)  ", 0}, {"37 ( -)  ", 0}, {"38 ( -)  ", 0}, {"39 ( -)  ", 0},
    {"40 ( -)  ", 0}, {"41 ( -)  ", 0}, {"42 ( -)  ", 0}, {"43 ( -)  ", 0}, {"44 ( -)  ", 0},
    {"45 ( -)  ", 0}, {"46 ( -)  ", 0}, {"47 ( -)  ", 0}, {"48 ( -)  ", 0}, {"49 ( -)  ", 0},
    {"50 ( -)  ", 0}, {"51 ( -)  ", 0}, {"52 ( -)  ", 0}, {"53 ( -)  ", 0}, {"54 ( -)  ", 0},
    {"55 ( -)  ", 0}, {"56 ( -)  ", 0}, {"57 ( -)  ", 0}, {"58 ( -)  ", 0}, {"59 ( -)  ", 0},
    {"60 ( -)  ", 0}, {"61 ( -)  ", 0}, {"62 ( -)  ", 0}, {"63 ( -)  ", 0}};

/* relabels the way-group selector: a helper between group_create and
   group_select, which inlines it at all three of its call sites */
static inline void relabel_way_groups(void) /* derived name */
{
    int n = 0;
    int i;

    for (i = 0; i < 94; i++) {
        if (way_group[i].used == 1) {
            sprintf(debugWayGroupSelect[n].s, "% 2d (% 2d) ", n, way_group[i].count);
            if (way_group[i].bridge == 1) {
                strcat(debugWayGroupSelect[n].s, "b");
            }
            n++;
        }
    }
}

/* a file-static group_select, distinct from camera-editor.o's global of the
   same name; debugWayMenu below holds its address */
static int group_select(void)
{
    static int selectState = 0; /* derived name */
    WayGroup *e;
    int state;
    int i;
    int r;

    state = selectState;
    if (state == 0) {
        relabel_way_groups();
        for (i = 0; i < 94; i++) {
            e = &way_group[i];
            if (e->used == 1) {
                if (i == current_select_gid) {
                    wayGroupSel = i;
                    break;
                }
            }
        }
        selectState = 1;
    } else if (state == 1) {
        if (wayToolPad.trg & 0x2000) {
            set_bridge(current_select_gid);
            relabel_way_groups();
        } else if (wayToolPad.trg & 0x8000) {
            way_group[current_select_gid].bridge = 0;
            relabel_way_groups();
        }
        r = debug_SelectCsvWindow("group + select", 0x12, 0x36, 0xB, debugWayGroupSelect, 8, 0, 1,
                                  n_way_group, &wayGroupSel);
        switch (r) {
        case 0:
            current_select_gid = wayGroupSel;
            return 0;
        case -1:
            selectState = 0;
            return -1;
        default:
            selectState = 2;
            break;
        }
    } else if (state == 2) {
        current_select_gid = wayGroupSel;
        selectState = 0;
        return -1;
    }
    return 0;
}

static int point_delete(void)
{
    WayGroup *entry = &way_group[current_select_gid];
    int f;

    if (debug_font_flag & 1) {
        debug_Printf(18, 54, 0xFF000000, "point + delete\n");
        if (debug_font_flag & 1) {
            debug_Printf(26, 66, 0xFF808000, "pt.%d", entry->count);
        }
    }
    f = wayToolPad.trg;
    if (f & 0x20) {
        WayPoint *res = waypoint_with_range(wayWorkPos, 60.0f);

        if (res == 0) {
            return 0;
        }
        {
            int n = res->index;

            wayPointSel = n;
            if (n >= 0) {
                DeleteWayPoint(n);
                if (entry->count == 0) {
                    DeleteWayGroup(current_select_gid);
                }
                debug_StdPrintfDummy("delete waypoint %d\n", wayPointSel);
                return 0;
            }
        }
    } else if (f & 0x40) {
        return -1;
    }
    return 0;
}

static int point_insert(void)
{
    static int insertState = 0; /* derived name */
    WayGroup *entry = &way_group[current_select_gid];
    int f;

    if (debug_font_flag & 1) {
        debug_Printf(18, 54, 0xFF000000, "point + insert\n");
        if (debug_font_flag & 1) {
            debug_Printf(26, 66, 0xFF808000, "pt.%d", entry->count);
        }
    }
    insertState = 1;
    f = wayToolPad.trg;
    if (!(f & 0x20)) {
        if (f & 0x40) {
            insertState = 0;
            return -1;
        }
        return 0;
    }
    {
        WayPoint *res = nearest_waypoint_by_lineseg(wayWorkPos);
        if (res->next == 0) {
            return 0;
        }
        {
            int n = CreateWayPoint(wayWorkPos);
            InsertWayPointAfter(current_select_gid, res->index, n);
            entry->count = entry->count + 1;
            debug_StdPrintfDummy("insert waypoint %d\n", n);
        }
    }
    return 0;
}

inline int play_way(void)
{
    static int playMode = 0; /* derived name */
    GObj *g;
    int f;

    if (debug_font_flag & 1) {
        debug_Printf(18, 54, 0xFF000000, "to boy\n");
    }
    f = wayToolPad.trg;
    if (f & 0x20) {
        g = isysGObjSearchFromObjKindID_begin(2);
        switch (playMode) {
        case 0:
            while (g != 0) {
                GOBJ_ACT(g)->wayMode = 1;
                g = isysGObjSearchFromObjKindID_next(g);
            }
            break;
        case 1:
            while (g != 0) {
                GOBJ_ACT(g)->wayMode = 0;
                g = isysGObjSearchFromObjKindID_next(g);
            }
            break;
        }
        playMode ^= 1;
    } else if (f & 0x40) {
        return -1;
    }
    return 0;
}

inline int point_nige(void)
{
    WayPoint *p;
    int v;

    if (debug_font_flag & 1) {
        unsigned int color = 0xFF000000;
        debug_Printf(18, 54, color, "point + nige\n");
    }
    v = wayToolPad.trg;
    if (v & 0x20) {
        p = waypoint_with_range(wayWorkPos, 60.0f);
        if (p == 0) {
            return 0;
        }
        wayPointSel = p->index;
        if (p->index >= 0) {
            p->escape ^= 1;
        }
    } else if (v & 0x40) {
        return -1;
    }
    return 0;
}

inline int quick_save_wpfile(void)
{
    char buf[112];
    int s0;
    int i;
    unsigned char *p;
    load_save_flag = 1;
    sprintf(buf, "test.wp");
    s0 = debugSceOpen(buf, 0x202);
    if (s0 < 0) {
        debug_StdPrintfDummy("cannot save wp file");
        load_save_flag = 0;
        return 0;
    }
    i = 0xF;
    p = &wpBuf[i];
    do {
        *p = i;
        p--;
        i--;
    } while (i >= 0);
    sceWrite(s0, wpBuf, 0x10);
    debugSceClose(s0);
    debug_StdPrintfDummy("saved\n");
    load_save_flag = 0;
    return 1;
}

static int quick_load_wpfile(void)
{
    char buf[112];
    int s0;
    int i;
    char *p;

    load_save_flag = 1;
    sprintf(buf, "test.wp");
    s0 = debugSceOpen(buf, 1);
    if (s0 < 0) {
        debug_StdPrintfDummy("cannot load wp file\n");
        load_save_flag = 0;
        return 0;
    }
    FlushCache(0);
    i = 0x1F;
    p = (char *)&wpBuf[i];
    do {
        *p = -1;
        p--;
        i--;
    } while (i >= 0);
    sceRead(s0, &wpBuf[15], 0x10);
    debugSceClose(s0);
    for (i = -15; i < 17; i++) {
        debug_StdPrintfDummy("%d ", ((char *)wpBuf)[i + 15]);
    }
    debug_StdPrintfDummy("\n");
    debug_StdPrintfDummy("loaded\n");
    load_save_flag = 0;
    return 1;
}

/* the authored way group table: one 0x3C record per group */
/* the authored way point table: one 0x1C record per point */
typedef struct { /* field names derived */
    float f[4];
} __attribute__((aligned(8))) WayPos; /* derived name */

void ExtractWayData(int stage_no)
{
    WayPos v;
    const WaySrcGrp *e;
    WaySrcPt *q;
    WayPoint *w;
    WayGroup *b;
    int start;
    int end;
    int i;
    int j;
    int g;
    int p;

    start = stageData[stage_no].wayGroupStart;
    end = stageData[stage_no].wayGroupEnd;

    for (i = start; i < end; i++) {
        e = &wayGroupSheet[i];
        g = CreateWayGroup();
        way_group[g].bridge = e->bridge;
        way_group[g].end[0] = e->bridgeEnd[0];
        way_group[g].end[1] = e->bridgeEnd[1];
        way_group[g].active = e->active;
        for (j = e->firstPoint; j < e->lastPoint; j++) {
            q = &wayPointSheet[j];
            {
                WayPos t;

                memset(&t, 0, 16);
                t.f[0] = -q->pos[0];
                t.f[1] = -q->pos[1];
                t.f[2] = -q->pos[2];
                v = t;
            }
            p = CreateWayPoint(v.f);
            AddWayPoint(g, p);
            w = &way_point[p];
            w->radius = q->radius;
            w->escape = q->escape;
            w->float2C = q->float14;
            w->bridgeEnd = q->bridgeEnd;
        }
        if (e->closed == 1) {
            CloseWayGroup(g);
        }
    }

    for (b = WayBridgeAll_begin(); b != 0; b = WayBridgeAll_next(b)) {
        set_bridge(b->index);
    }

    n_way_group = end - start;
    current_select_gid = 0;
}

/* the editable way-file base name in .sdata */
typedef struct { /* field names derived */
    char s[8];
} WpName; /* derived name */

static int wp_print_out(void)
{
    WpName name = {"way0000"};
    char line[256];
    char fname[112];
    WayGroup *g;
    WayPoint *p;
    int fd;
    int n;

    load_save_flag = 1;
    sprintf(fname, "%s.txt", name.s);
    fd = debugSceOpen(fname, 0x602);
    if (fd < 0) {
        debug_StdPrintfDummy("cannot open file");
        load_save_flag = 0;
        return 0;
    }
    sprintf(line, "equn\t\t%s_start\n", name.s);
    sceWrite(fd, line, strlen(line));
    for (n = 0, g = WayGroup_begin(); g != 0; g = WayGroup_next(g)) {
        if (g->boxBridge != 1) {
            sprintf(line, "\t%d\t%d\t%s_%d_start\t%s_%d_end\t%d\t%d\t%d\t%d\n", n, n, name.s, n,
                    name.s, n, g->closed, g->bridge, -1, -1);
            sceWrite(fd, line, strlen(line));
            n++;
        }
    }
    sprintf(line, "equn\t\t%s_end\n", name.s);
    sceWrite(fd, line, strlen(line));
    for (n = 0, g = WayGroup_begin(); g != 0; g = WayGroup_next(g)) {
        if (g->boxBridge != 1) {
            sprintf(line, "equn\t%s_%d_start\n", name.s, n);
            sceWrite(fd, line, strlen(line));
            for (p = WayPointList_begin(g->index); p != 0; p = WayPointList_next(p)) {
                sprintf(line, "\t\t\t%d\t%d\t%d\t\t%d\t%d\n", (int)-p->pos[0], (int)-p->pos[1],
                        (int)-p->pos[2], (int)p->radius, p->escape);
                sceWrite(fd, line, strlen(line));
            }
            sprintf(line, "equn\t%s_%d_end\n", name.s, n);
            sceWrite(fd, line, strlen(line));
            n++;
        }
    }
    debugSceClose(fd);
    load_save_flag = 0;
    return -1;
}

typedef struct { /* field names derived */
    float f[4];
} __attribute__((aligned(8))) WayVec; /* derived name */

/* way_tool.o .data +0x210: the nine RGBA packets the tool draws with. */
static WayCol wayColorSelected = {{0xFF, 0xFF, 0xFF, 0xFF}}; /* derived name */

static WayCol wayColorLinked = {{0xFF, 0x08, 0xFF, 0xFF}}; /* derived name */

static WayCol wayColorBlink = {{0xFF, 0xFF, 0xFF, 0xFF}}; /* derived name */

static WayCol wayColorCursor = {{0x80, 0xFF, 0x1E, 0xFF}}; /* derived name */

static WayCol wayColorOpenCurrent = {{0x20, 0xFF, 0x20, 0xFF}}; /* derived name */

static WayCol wayColorOpenOther = {{0x20, 0x80, 0x20, 0x30}}; /* derived name */

static WayCol wayColorClosedCurrent = {{0x40, 0x40, 0x00, 0xFF}}; /* derived name */

static WayCol wayColorClosedOther = {{0x40, 0x40, 0x00, 0x40}}; /* derived name */

static WayCol wayColorBridge = {{0xFF, 0x00, 0xFF, 0xFF}}; /* derived name */

static inline void set_way_point_color(WayPoint *p, WayCol *col) /* derived name */
{
    WayCol *d = &wayDrawCol;

    if (p->escape != 0) {
        *d = wayColorLinked;
    } else if (p->index == wayPointSel) {
        *d = wayColorSelected;
    } else {
        *d = *col;
    }
    if (p->bridgeEnd != 0 && (((unsigned int)frame_count) & 0x10)) {
        *d = wayColorBlink;
    }
}

static void draw_way_group(int g, WayCol *col)
{
    WayGroup *e = &way_group[g];
    WayVec m;
    WayVec blink;
    WayPoint *p;
    float *q;

    memset(&blink, 0, 16);
    blink.f[1] = (float)((unsigned int)frame_count) * 0.116355285f;
    m = blink;

    p = e->first;
    while (p != 0) {
        q = p->pos;
        SetVObjRT(&m, q);
        set_way_point_color(p, col);
        DrawVObj(0, &wayDrawCol);
        if (p->next != 0) {
            gif_StartPacketPri(11);
            sceVu0UnitMatrix(MatrixDrive_GetMatrix());
            DrawLine(q, p->next->pos, col, 0x800000);
            gif_EndPacket();
        }
        if (p->next == e->first) {
            break;
        }
        p = p->next;
    }
}

static void way_toolDL(GObj *self)
{
    WayVec m;
    WayVec blink;
    WayVec pp;
    WayGroup *e;
    WayPoint *w;
    int i;

    if (debug_wayline == 0) {
        return;
    }
    if (load_save_flag != 0) {
        return;
    }
    GetRootPosition(wayWorkPos, self);

    MatrixDrive_PushMatrix();
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());

    memset(&blink, 0, 16);
    blink.f[1] = (float)((unsigned int)frame_count) * 0.116355285f;
    m = blink;
    SetVObjRT(&m, wayWorkPos);
    DrawVObj(0, &wayColorCursor);

    for (i = 0; i < 94; i++) {
        e = &way_group[i];
        if (e->used == 1) {
            if (e->active != 0) {
                if (i == current_select_gid) {
                    draw_way_group(i, &wayColorOpenCurrent);
                } else {
                    draw_way_group(i, &wayColorOpenOther);
                }
            } else {
                if (i == current_select_gid) {
                    draw_way_group(i, &wayColorClosedCurrent);
                } else {
                    draw_way_group(i, &wayColorClosedOther);
                }
            }
            if (e->bridge == 1) {
                sceVu0UnitMatrix(MatrixDrive_GetMatrix());
                gif_StartPacketPri(11);
                if (e->end[0] != -1) {
                    DrawLine(e->first->pos, way_point[e->end[0]].pos, &wayColorBridge, 0x800000);
                    DrawLine(e->last->pos, way_point[e->end[1]].pos, &wayColorBridge, 0x800000);
                }
                gif_EndPacket();
            }
        }
    }
    MatrixDrive_PopMatrix();

    GetRootPosition(&blink, boyGObj);
    GetRootProjectionPosOfGObj(pp.f, boyGObj);
    w = visible_waypoint_of_all(&pp);
    if (w != 0) {
        ez_circle(w->pos, &blink, 0x80800080, 20.0f);
    }
}

typedef struct { /* field names derived */
    char *name;
    int (*fn)();
} WayMenu; /* derived name */

/* way_tool.o .data +0x2A0: the way-tool menu, nine {label, action} lines.
   Line 5's label is the play/stop text the tool rewrites at runtime. */

WayMenu debugWayMenu[9] = {{"group + create", group_create},  {"      + select", group_select},
                           {"point + delete", point_delete},  {"      + insert", point_insert},
                           {"      + nige", point_nige},      {"play", play_way},
                           {"quick save", quick_save_wpfile}, {"quick load", quick_load_wpfile},
                           {"save text", wp_print_out}};

int debug_WayTool(void)
{
    static int menuState = 1; /* derived name */
    static int menuSel = 0;   /* derived name */
    float pos[4];
    int r;
    int (*f)(int);
    int state;

    cursorGObj = isysGObjSearchFromObjLayoutID(2);
    if (cursorGObj != 0) {
        if (first_waytool == 0) {
            *(void **)&cursorGObj->act =
                iosMallocDebug(ios_partition_seki, ICO_MAX_SIZE(Act, 0x850), __FILE__, 0x4AA);
            isysGObjProcAdd(cursorGObj, cursor_control, 0, 0x13);
            isysGObjLinkObjDL(cursorGObj, way_toolDL, 0, 0, 0xFFFFFFFF);
            first_waytool = 1;
        }
    }

    if (first_waytool == 1) {
        savedCamTarget = CurrentTargetGObj;
        CurrentTargetGObj = cursorGObj;
        GetRootPosition(pos, savedCamTarget);
        SetDirectRootPosition(CurrentTargetGObj, pos);
        Camctrl_SetTarget(CurrentTargetGObj, 0, 3);
        first_waytool = 2;
    }

    iosPadConnect(&wayToolPad, 0, 0, &iosPadConfDefault);
    iosPadRead(&wayToolPad);
    iosPadGetStick(&wayToolPad, wayToolStick, 1, 0, 0, 0);

    state = menuState;
    if (state == 1) {
        r = debug_SelectCsvWindow("Way Tool", 0x12, 0x36, 0xB, debugWayMenu, 8, 0, 1, 9, &menuSel);
        switch (r) {
        case 0:
            return 0;
        case -1:
            first_waytool = 1;
            menuState = 1;
            CurrentTargetGObj = savedCamTarget;
            Camctrl_SetTarget(CurrentTargetGObj, 0, 3);
            return -1;
        default:
            first_waytool = 1;
            menuState = 2;
            break;
        }
    } else if (state == 2) {
        f = debugWayMenu[menuSel].fn;
        if (f == 0) {
            menuState = 1;
        } else {
            r = f(first_waytool);
            if (r == -1) {
                menuState = 1;
            } else if (r != 0) {
                menuState = 1;
            }
        }
    }
    return 0;
}

inline void cursor_control(GObj *volatile self)
{
    Act *w = GOBJ_ACT(self);

    iosPadConnect(&w->pad, 0, 0, &iosPadConfDefault);

    while (1) {
        iosPadRead(&w->pad);

        if (self == CurrentTargetGObj && (w->pad.trg & 1)) {
            ACTDebugMove(self, 1);
        }
        _ACTWait(1);
    }
}
