#include "typedef.h"
#include "debug.h"
#include "Texture.h"
#include "debug_exception.h"
#include "layout_texture.h"
#include <assert.h>
#include "charFileManager.h"
#include "kanban.h"

typedef union { /* field names derived */
    int i[4];
    char b[16];
} Pkt16; /* derived name */

/* The list head and the sign the layout key follows (.sbss), and the pool
   (.bss) of thirty signs that kanbanReqAdd and kanbanReqAllDel walk. */
static Kanban *kanbanList; /* derived name */

static Kanban *kanbanCurrent; /* derived name */

static Kanban kanbanNodes[30]; /* derived name */

/* kanban quest box over */
static const char kanbanOverMsg[] = "かんばんクエストボックスオーバー\n"; /* derived name */

/* The sprite the kanban is drawn as, {x, y, width, height} centred on the
   origin, the same rectangle src/staffroll.c uses for the roll. */
static const Pkt16 kanbanSprite = {{-5120, -1792, 10240, 3584}}; /* derived name */

/* .sdata: kanbanCommonRead and the sign's initial colour */
int kanbanCommonRead = 0;

/* the colour a new sign starts with */
static KanbanCol kanbanStartCol = {{0x80, 0x80, 0x80, 0}}; /* derived name */

/* texProperty's texNo column, &texProperty[0].texNo */
#define D_0030D014 ((char *)&texProperty[0].texNo)

/* a file static, as are the functions of the same name in
   ico2/fumi/src/jimaku and ico2/common/src/layout_texture */
static void display_texture(LtProp *pr, LtProperty *e, KanbanCol *col);
/* sce/'s string.h does not declare it */
extern char *strtok(char *s, const char *sep);
/* GifPacket.h's entry points, which this TU does not include; the sprite
   calls take z as an unsigned int here, a long long in the header */
extern void gif_EndPacket(void);
extern void gif_SetAlpha(long long alpha, long long mode, long long fix);
extern void gif_SetZTest(int on);
extern void gif_SetZWrite(int on);

/* PC port (v0.4.2): the menu text hook (port/ui/layout_ext.h) */
#include "layout_ext.h"

#ifdef ICO_RD

/* PC port (package TXT): GifHost.h's key of the decoder's primitives, so
   the presenter matches a sign's sprites between ticks by its row (its
   texProperty entry) and the backdrop by its packet, and blends their
   fades.  The cursor sparkle stays unkeyed (its points are random each
   tick). */
extern void gif_HostDrawKey(const void *obj, int part, int ordinal);

#define KANBAN_HOST_KEY(obj, part) gif_HostDrawKey((obj), (part), 0)
#else
#define KANBAN_HOST_KEY(obj, part) ((void)0)
#endif

/* ICO_HOST: GifPacket.c's parameter types, so arguments land where the
   definition reads them on hosts that pass them on the stack
   (layout_texture.c says more) */

extern void gif_SpriteSensitive(int *r, long long z, int *uv, unsigned char *col, int prim);
extern void gif_SpriteSensitiveOffset(int *r, long long z, int *uv, unsigned char *col, int prim);
extern void gif_PointOffset(int *v, long long z, unsigned char *col, int prim);
extern void gif_StartPacketPri(int pri);

#include <string.h>
#include <stdlib.h>
#include "main.h"

static void init_textures_of_specified_property(int first, int last);

static inline char *get_texture_base_name(char *src) /* derived name */
{
    /* the result points into it, so on the host it outlives the call */
    static char buf[256];
    char *p;
    char *t;

    p = buf;

    strcpy(buf, src);

    t = strtok(buf, "/");
    if (t != 0) {
        do {
            p = t;
            t = strtok(0, "/");
        } while (t != 0);
    }
    if ((t = strrchr(p, '.')) != 0) {
        *t = 0;
    }
    return p;
}

static inline int get_texture_no_of_property(int idx) /* derived name */
{
    int n;
    char *src;
    char *name;
    int no;

    n = texProperty[idx].texFileNo;
    src = texFile[n].path;
    name = get_texture_base_name(src);

    no = tex_GetTextureNo(name);

    if (no < 0) {
        debug_StdPrintfDummy("tex_id %d\n", n);
        debug_StdPrintfDummy("no texture loaded.(%s)\n", src);
        debug_assert(__FILE__, 272);
        __assert(__FILE__, 272, "0");
    }
    return no;
}

static inline void init_textures_of_property_range(int first, int last) /* derived name */
{
    int i;

    for (i = first; i < last; i++) {
        init_textures_of_specified_property(texLayout[i].first, texLayout[i].last);
    }
}

static inline int kanban_layout_key(LtProp *pr) /* derived name */
{
    int ret = 0;
    LtProperty *e = &texProperty[pr->curItem];

    if ((pad[0].flags & 0x1000) && e->upItem > 0) {
        pr->curItem = e->upItem;
    } else if ((pad[0].flags & 0x4000) && e->downItem > 0) {
        pr->curItem = e->downItem;
    } else if ((pad[0].flags & 0x8000) && e->leftItem > 0) {
        pr->curItem = e->leftItem;
    } else if ((pad[0].flags & 0x2000) && e->rightItem > 0) {
        pr->curItem = e->rightItem;
    } else {
        unsigned long long button = pad[0].flags;

        if (button & 0x40) {
            ret = 1;
        } else {
            ret = (button & 0x10) ? 2 : 0;
        }
    }
    return ret;
}

inline void kanbanReqAllDel(void)
{
    int i;
    for (i = 29; i >= 0; i--) {
        kanbanNodes[i].layout = 0;
    }
    kanbanList = 0;
    kanbanCurrent = 0;
}

Kanban *kanbanReqAdd(int no, int pri)
{
    Kanban *p;
    LtProp *pr;
    Kanban *cur;
    int i;

    p = kanbanNodes;
    pr = &texLayout[no];
    for (i = 0; i < 30; i++, p++) {
        if (p->layout == 0)
            goto found;
    }
    debug_StdPrintfDummy(kanbanOverMsg);
    return 0;

found:
    p->layout = pr;
    p->key = 0;
    pr->curItem = pr->defaultItem;
    p->flags &= ~1;
    p->alpha = 0;
    p->col = kanbanStartCol;
    p->pri = pri;
    cur = kanbanList;
    if (cur != 0) {
        if (pri < cur->pri) {
            cur->prev = p;
            p->next = cur;
            p->prev = 0;
            kanbanList = p;
        } else {
            for (;;) {
                if (cur->next == 0) {
                    goto append;
                }
                if (pri < cur->pri) {
                    break;
                }
                cur = cur->next;
            }
            p->prev = cur->prev;
            cur->prev = p;
            p->next = cur;
            goto done;
        append:
            cur->next = p;
            p->prev = cur;
            p->next = 0;
        }
    } else {
        kanbanList = p;
        p->prev = 0;
        p->next = 0;
    }
done:
    if (pr->defaultItem != -1) {
        kanbanCurrent = p;
    }
    return p;
}

inline void kanbanReqDel(Kanban *self)
{
    Kanban *prev = self->prev;
    Kanban *next = self->next;
    if (prev == 0) {
        kanbanList = next;
        if (next != 0) {
            next->prev = 0;
        }
    } else {
        prev->next = next;
        if (next != 0) {
            self->next->prev = self->prev;
        }
    }
    self->layout = 0;
}

inline void kanbanReqDelFade(Kanban *self)
{
    Kanban *cur = kanbanCurrent;
    self->flags |= 1;
    if (self == cur) {
        kanbanCurrent = 0;
    }
}

inline void kanbanReqAllDelFade(void)
{
    Kanban *p = kanbanNodes;
    int i = 29;
    do {
        if (p->layout != 0) {
            p->flags |= 1;
        }
        i--;
        p++;
    } while (i >= 0);
}

static void init_textures_of_specified_property(int first, int last)
{
    int i;
    int no;

    for (i = first; i < last; i++) {
        debug_StdPrintfDummy("propertyId %d\n", i);
        no = get_texture_no_of_property(i);
        /* the EE's stride and texData-before-texNo offset are 32-bit only */
        texProperty[i].texNo = no;
        texProperty[i].texData = tex_GetTextureData(no);
        tex_SetSamplingType(texProperty[i].texData, 1, 1);
    }
}

void kanbanInit(int no)
{
    if (no != 0) {
        init_textures_of_property_range(stageData[no].layoutFirst, stageData[no].layoutLast);
    } else {
        init_textures_of_property_range(0, 1);
        kanbanReqAllDel();
        kanbanCommonRead = 1;
    }
}

static void display_texture(LtProp *pr, LtProperty *e, KanbanCol *col)
{
    int uv[4];
    int r[4];
    int pt[4];
    int i;
    int alpha;

    uv[0] = (e->texU << 4) + 8;
    uv[1] = (e->texV << 4) + 8;
    uv[2] = e->texW << 4;
    uv[3] = e->texH << 4;

    r[2] = e->dispW << 4;
    r[3] = e->dispH << 3;
    if (r[2] == 0) {
        r[2] = uv[2];
    }
    if (r[3] == 0) {
        r[3] = uv[3] >> 1;
    }

    if (e->centerX) {
        r[0] = (0x2800 - r[2]) / 2 - 0x1400;
    } else {
        r[0] = (e->dispX - 320) << 4;
    }
    r[1] = (e->dispY - 112) << 4;

    if (e->masked == 0) {
        tex_TransTexture(e->texNo, 11);

        gif_StartPacketPri(11);

        gif_SetAlpha(1, 7, 0);

        gif_SetZWrite(0);

        r[1] += 8;

        r[3] -= 8;
        uv[3] -= 8;

        /* PC port (v0.4.2): a menu text row (the boot screens' prompts,
           Yes / No, the language names) is drawn in the sheets' look where
           the sprite would be, its texture transferred as before
           (port/ui/menu_text.h) */
        KANBAN_HOST_KEY(e, 0);
        if (lt_ext_is_text_row(e)) {
            lt_ext_draw_text_row(e, r, uv, (const unsigned char *)col->b, 0);
        } else
            gif_SpriteSensitiveOffset(r, 0xFFFFFF9B, uv, col->b, 1);
        KANBAN_HOST_KEY(0, 0);
        gif_SetZWrite(1);
        gif_EndPacket();
    }

    if (e == &texProperty[pr->curItem]) {
        KanbanCol col2 = {{0x80, 0x80, 0x80, 0x7F}};

        gif_StartPacketPri(11);
        gif_SetZTest(0);

        for (i = 0; i < r[2] * r[3] / 300; i++) {
            pt[0] = r[0] + rand() % r[2];
            pt[1] = r[1] + rand() % r[3];
            alpha = rand() % 127 + 32;
            col2.b[3] = alpha;
            if (col->b[3] < col2.b[3]) {
                col2.b[3] = col->b[3];
            }

            gif_PointOffset(pt, 0x800000, col2.b, 1);
        }
        gif_EndPacket();
    }
}

static int fade_exec(Kanban *p)
{
    int ret = 0;
    float f;

    if ((p->flags & 1) == 0) {
        f = 127.0f /
            (p->layout->fadeInTime * (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
        if (f == 0.0f) {
            f = 127.0f;
        }

        p->alpha = p->alpha + f;
        if (p->alpha > 127.0f) {
            p->alpha = 127.0f;
            ret = 1;
        }
    } else {
        f = 127.0f /
            (p->layout->fadeOutTime * (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
        if (f == 0.0f) {
            f = 127.0f;
        }
        p->alpha = p->alpha - f;
        if (p->alpha < 0.0f) {
            p->alpha = 0.0f;
            ret = -1;
        }
    }
    p->col.b[3] = (char)p->alpha;
    return ret;
}

static void display_layout(Kanban *k)
{
    LtProp *pr;
    int i;

    pr = k->layout;

    k->key = 0;
    if (kanbanCurrent != 0 && kanbanCurrent->layout == pr && (k->flags & 1) == 0) {
        k->key = kanban_layout_key(pr);
    }

    if (fade_exec(k) < 0) {
        kanbanReqDel(k);
    } else {
        for (i = pr->first; i < pr->last; i++) {
            display_texture(pr, &texProperty[i], &k->col);
        }
    }
}

inline void kanbanExec(void)
{
    Kanban *k;
    unsigned char col[4];
    Pkt16 pkt;

    if (kanbanList != 0) {
        LtProp *pr = kanbanList->layout;
        col[0] = (int)(pr->colR * 255.0f);
        col[1] = (int)(pr->colG * 255.0f);
        col[2] = (int)(pr->colB * 255.0f);
        col[3] = (int)(pr->colA * 127.0f);
        gif_StartPacketPri(11);
        gif_SetZTest(0);
        gif_SetZWrite(0);
        gif_SetAlpha(1, 7, 0);
        pkt = kanbanSprite;
        KANBAN_HOST_KEY(&kanbanSprite, 0);
        gif_SpriteSensitive(pkt.i, 0xFFFFFFFFu, 0, col, 1);
        KANBAN_HOST_KEY(0, 0);
        gif_SetZWrite(1);
        gif_SetZTest(1);
        gif_EndPacket();
    }
    k = kanbanList;
    if (k != 0) {
        do {
            display_layout(k);
            k = k->next;
        } while (k != 0);
    }
}
