#include "typedef.h"
#include "layout_texture.h"
#include "debug.h"
#include "StageManager.h"
#include "gflag.h"
#include "layout_action.h"
#include <string.h>
#include <stdlib.h>
#include "debug_exception.h"
#include "tableSin.h"
#include "s_init.h"
#include <assert.h>
#include "charFileManager.h"
#include "main.h"

typedef struct { /* field names derived */
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} SprCol; /* derived name */

/* .sdata: the continue screen's decided flag, which
   layout_action sets and op's countdown waits on; the two highlight colours;
   the current layout; the selected item; the item-select handler's flag; the
   fade state and type; lt_item_select_disable; the fade-end handler; the
   highlight blink's count and length. */
int lt_continue_selected = 0; /* derived name */

static unsigned char ltCursorColor[4] = {128, 128, 128, 127}; /* derived name */

static SprCol ltHighlightColor = {128, 128, 128, 127}; /* derived name */

int current_layout_id = 0;

static unsigned int ltCurrentItem = -1; /* derived name */

static int ltSelectFlag = 1; /* derived name */

static int fadeState = 0; /* derived name */

static int fadeType = 0; /* derived name */

int lt_item_select_disable = 0;

static ICO_WORD_PTR(LtSelectFn) fadeCallback = 0; /* derived name */

static unsigned int ltBlinkCount = 0; /* derived name */

static unsigned int ltBlinkLength = 0; /* derived name */

/* .sbss, nine words: the pad buttons of the last
   frame, the layout switched to and the fade state that follows the switch,
   the selection glow's flag, count and length, the frame of the last
   selection, and the switch fade's length and count. */
static int lastButton; /* derived name */

static int nextLayout; /* derived name */

static int nextFadeState; /* derived name */

static signed char glowOn; /* derived name */

static unsigned int glowCount; /* derived name */

static unsigned int glowLength; /* derived name */

static int selectFrame; /* derived name */

static unsigned int fadeLength; /* derived name */

static unsigned int fadeCount; /* derived name */

/* memset with an int count, which this TU's calls pin over <string.h>'s
   unsigned one */
#ifndef ICO_HOST

extern void *memset(void *dst, int c, int n);

#endif

#include "Texture.h"

/* PC port (Phase 6, 6B): the rows past the game's tables.  texLayout[80] and
   texProperty[436] are fixed-size runtime-loaded arrays with no free
   property row; the port's Settings rows live in an extension past their
   ends (port/ui/layout_ext.h), which this file's lookups fall through to.
   The texture initialisation below walks the stages' layout ranges, which
   never reach an extension index, and keeps the plain arrays. */
#ifdef ICO_HOST

#include "layout_ext.h"

#define LT_LAYOUT(i) (*lt_ext_Layout(i))
#define LT_PROP(i) (*lt_ext_Prop(i))
#else
#define LT_LAYOUT(i) texLayout[i]
#define LT_PROP(i) texProperty[i]
#endif

static void default_item_select(int no);

/* The sprite rectangle the gif helpers take: origin and size, in 1/16 pixels. */
typedef struct { /* field names derived */
    int x;
    int y;
    int w;
    int h;
} SprRect; /* derived name */

/* the screen rectangle lt_draw_primary_sprite draws, in 1/16 pixels: 640 x 226
   pixels centred on the origin */
static const SprRect primarySpriteRect = {-5120, -1808, 10240, 3616}; /* derived name */

static void display_texture_fade_cancel_chk(int from, int to)
{
    short list1[256];
    short list2[256];
    int i, j;
    int k, l;
    int n1 = 0;
    int n2 = 0;

    for (i = from; i >= 0; i = LT_LAYOUT(i).link) {
        for (j = LT_LAYOUT(i).first; j < LT_LAYOUT(i).last; j++) {
            LtProperty *pr = &LT_PROP(j);

            pr->fade_cancel = 0;
            list1[n1++] = j;
        }
    }
    for (i = to; i >= 0; i = LT_LAYOUT(i).link) {
        for (j = LT_LAYOUT(i).first; j < LT_LAYOUT(i).last; j++) {
            LtProperty *pr = &LT_PROP(j);

            pr->fade_cancel = 0;
            list2[n2++] = j;
        }
    }
    for (k = 0; k < n1; k++) {
        LtProperty *p = &LT_PROP(list1[k]);

        for (l = 0; l < n2; l++) {
            LtProperty *q = &LT_PROP(list2[l]);

            if (p->texFileNo == q->texFileNo && p->texU == q->texU && p->texV == q->texV &&
                p->texW == q->texW && p->texH == q->texH && p->dispX == q->dispX &&
                p->dispY == q->dispY && p->dispW == q->dispW && p->dispH == q->dispH) {
                q->fade_cancel = 1;
                p->fade_cancel = 1;
            }
        }
    }
}

void lt_analog2Pad(void)
{
    if (pad[0].ana[3] < 20) {
        pad[0].now |= 0x1000;
        if ((lastButton & 0x1000) == 0) {
            pad[0].flags |= 0x1000;
        }
    }
    if (pad[0].ana[3] >= 236) {
        pad[0].now |= 0x4000;
        if ((lastButton & 0x4000) == 0) {
            pad[0].flags |= 0x4000;
        }
    }
    if (pad[0].ana[2] < 20) {
        pad[0].now |= 0x8000;
        if ((lastButton & 0x8000) == 0) {
            pad[0].flags |= 0x8000;
        }
    }
    if (pad[0].ana[2] >= 236) {
        pad[0].now |= 0x2000;
        if ((lastButton & 0x2000) == 0) {
            pad[0].flags |= 0x2000;
        }
    }
    lastButton = pad[0].now;
}

/* a file static (src/jimaku has a global of the same name and src/kanban a
   file static) */
static void display_texture(int no, LtProperty *e);

/* whether property item no is shown; the second range is a conditional
   expression */
static inline int lt_property_visible(int no) /* derived name */
{
    int vis = 1;

    if (gFlagGameClear == 0 && current_layout_id == 58 && no >= 300 &&
        (no < 308 || (no < 330 ? no >= 325 : 0))) {
        vis = 0;
    }
    return vis;
}

static inline void lt_draw_layout(int no) /* derived name */
{
    int i = LT_LAYOUT(no).first;
    int last = LT_LAYOUT(no).last;

    for (; i < last; i++) {
        if (lt_property_visible(i)) {
            display_texture(no, &LT_PROP(i));
        }
    }
}

/* lt_switch_layout's body as default_item_select's two call sites have it.
   The copies differ: the first site's else arm sets fadeState to 7 where the
   out-of-line function and the second site set 3. */
static inline void lt_switch_layout_7(int no) /* derived name */
{
    if ((fadeState == 2 && no != current_layout_id) || no == 62) {
        nextLayout = no;
        display_texture_fade_cancel_chk(current_layout_id, no);
        if (fadeType == 1) {
            fadeState = 5;
        } else {
            nextFadeState = 3;
            fadeState = 7;
        }
    }
}

static inline void lt_switch_layout_3(int no) /* derived name */
{
    if ((fadeState == 2 && no != current_layout_id) || no == 62) {
        nextLayout = no;
        display_texture_fade_cancel_chk(current_layout_id, no);
        if (fadeType == 1) {
            fadeState = 5;
        } else {
            nextFadeState = 3;
            fadeState = 3;
        }
    }
}

static void default_item_select(int no)
{
    LtProp *p = &LT_LAYOUT(no);
    LtProperty *e = &LT_PROP(p->curItem);
    int prev;

    if (p->curItem < 0) {
        return;
    }
    if (lt_item_select_disable != 0) {
        return;
    }
    if (fadeState != 2) {
        return;
    }
    if (fadeCallback == 0) {
        lt_analog2Pad();
        prev = p->curItem;
        if ((pad[0].flags & 0x50) == 0) {
            if ((pad[0].flags & 0x1000) && e->upItem >= 0) {
                p->curItem = e->upItem;
                while (!lt_property_visible(p->curItem)) {
                    p->curItem = LT_PROP(p->curItem).upItem;
                }
            } else if ((pad[0].flags & 0x4000) && e->downItem >= 0) {
                p->curItem = e->downItem;
                while (!lt_property_visible(p->curItem)) {
                    p->curItem = LT_PROP(p->curItem).downItem;
                }
            } else if ((pad[0].flags & 0x8000) && e->leftItem >= 0) {
                p->curItem = e->leftItem;
            } else if ((pad[0].flags & 0x2000) && e->rightItem >= 0) {
                p->curItem = e->rightItem;
            }
        }
        if (p->curItem != prev) {
            soundSeDefPlay(411, 0xFFFFFFFE, 0, 0);
            glowOn = 1;
            glowLength = (unsigned int)((60 - systemStatus[0] * 10) / systemStatus[1] * 0.25f);
            selectFrame = frame_count;
            glowCount = 0;
        }
    } else {
        p->curItem = ((int (*)(void))fadeCallback)();
        fadeCallback = 0;
    }

    e = &LT_PROP(p->curItem);
    if (pad[0].flags & 0x40) {
        if (e->right >= 0) {
            if (fadeState == 2) {
                soundSeDefPlay(412, 0xFFFFFFFE, 0, 0);
                lt_switch_layout_7(e->right);
                return;
            }
        }
    }
    if (pad[0].flags & 0x10) {
        if (e->left >= 0) {
            if (fadeState == 2) {
                soundSeDefPlay(413, 0xFFFFFFFE, 0, 0);
                lt_switch_layout_3(e->left);
            }
        }
    }
}

static inline void lt_reset_property_chain(int no) /* derived name */
{
    LtProp *p = &LT_LAYOUT(no);
    int i = p->link;

    while (i >= 0) {
        p = &LT_LAYOUT(i);
        p->curItem = p->defaultItem;
        p->procFirst = 1;
        i = p->link;
    }
}

/* the layout fade's state machine; fadeState is read and written as the
   file static itself, as in the rest of this TU */
static void texture_fading(LtProp *p)
{
    unsigned char *col = ltCursorColor;
    int *cur;

    switch (fadeType) {
    case 1:
        if (fadeState < 2) {
            if (fadeState >= 0) {
                fadeState = 2;
            }
        }
        break;
    case 0:
        switch (fadeState) {
        case 0:
            if (p->fadeInTime == 0.0f) {
                fadeState = 2;
            }
            break;
        case 3:
            if (p->fadeOutTime == 0.0f) {
                fadeState = 6;
                col[3] = 127;
            }
            break;
        }
        break;
    }
    switch (fadeState) {
    case 0:
        fadeState = 1;
        ltBlinkLength = (int)(p->fadeInTime * ((60 - systemStatus[0] * 10) / systemStatus[1]));
        ltBlinkCount = ltBlinkLength;
        /* fall through */
    case 1:
        col[3] = (ltBlinkLength - ltBlinkCount) * 127 / ltBlinkLength;
        ltBlinkCount--;
        if (ltBlinkCount == 0) {
            fadeState = 2;
        }
        break;
    case 2:
        col[3] = 127;
        break;
    case 7:
        fadeState = 8;
        fadeLength = (unsigned int)((60 - systemStatus[0] * 10) / systemStatus[1] * 0.25f);
        fadeCount = 0;
        /* fall through */
    case 8:
        if (++fadeCount >= fadeLength) {
            fadeState = nextFadeState;
        }
        break;
    case 3:
        fadeState = 4;
        ltBlinkLength = (int)(p->fadeOutTime * ((60 - systemStatus[0] * 10) / systemStatus[1]));
        ltBlinkCount = ltBlinkLength;
        break;
    case 4:
        col[3] = ltBlinkCount * 127 / ltBlinkLength;
        ltBlinkCount--;
        if (ltBlinkCount == 0) {
            fadeState = 5;
        }
        break;
    case 5:
        col[3] = 0;
        /* fall through */
    case 6:
        current_layout_id = nextLayout;
        cur = &LT_LAYOUT(current_layout_id).curItem;
        *cur = LT_LAYOUT(current_layout_id).defaultItem;
        ltSelectFlag = 1;
        fadeState = 0;
        if (LT_LAYOUT(current_layout_id).fadeInTime == 0.0f) {
            lt_item_select_disable = 1;
            fadeState = 2;
        }
        lt_reset_property_chain(current_layout_id);
        break;
    }
    if (glowOn != 0) {
        if (glowCount++ >= glowLength) {
            glowOn = 0;
        }
    }
}

/* GifPacket.h's entry points, which this TU does not include: its calls pass
   the sprites' z as a 32-bit unsigned int, where the header takes a long
   long */
extern void gif_StartPacketPri(int pri);
extern void gif_SetZTest(int on);
extern void gif_SetZWrite(int on);
extern void gif_SetAlpha(long long alpha, long long mode, long long fix);

#ifdef ICO_HOST

/* The host passes z as GifPacket.c defines it: on the EE each argument has
   its own 64-bit register, so the narrower declaration only changed how the
   constant was loaded; on a host that passes arguments on the stack (i386)
   it shifts uv, col and prim by a word.  Only z's low 32 bits are used
   (GIF_XY), so the value is the same. */
extern void gif_SpriteSensitive(SprRect *r, long long z, SprRect *uv, SprCol *col, int prim);
extern void gif_EndPacket(void);
extern void gif_SpriteSensitiveOffset(SprRect *r, long long z, SprRect *uv, SprCol *col, int prim);

#else

extern void gif_SpriteSensitive(SprRect *r, unsigned int z, SprRect *uv, SprCol *col, int prim);
extern void gif_EndPacket(void);
extern void gif_SpriteSensitiveOffset(SprRect *r, unsigned int z, SprRect *uv, SprCol *col,
                                      int prim);

#endif

extern void gif_PointOffset(int *v, long long z, SprCol *col, int prim);
extern void gif_SetGsReg(long long reg, long long data);

#ifdef ICO_HOST

/* PC port (6B): the port row display_texture is drawing, whose glow is its
   label stretched like the texture sprite (port/ui/layout_ext.h) */
static LtProperty *ltHostTextRow;

#endif

/* the pulsing highlight sprite, inlined three times by display_texture */
static inline void lt_glow_sprite(SprRect *box, SprRect *ofs, int r, int g, int b, float t, int dx,
                                  int dy) /* derived name */
{
    SprRect rr = *box;
    SprCol c = {r, g, b, 127};
    float s = GetTableSin((short)(t * 3.1415926535897932 * 10430.3779296875));

    c.r = c.r * s;
    c.g = c.g * s;
    c.b = c.b * s;
    rr.x = rr.x - s * dx;
    rr.y = rr.y - s * dy;
    rr.w = rr.w + s * dx * 2;
    rr.h = rr.h + s * dy * 2;
    gif_SetAlpha(1, 5, 0);
#ifdef ICO_HOST
    if (ltHostTextRow != 0) {
        lt_ext_DrawRow(ltHostTextRow, (const int *)&rr, (const unsigned char *)&c, 1);
        return;
    }
#endif
    gif_SpriteSensitiveOffset(&rr, 0xFFFFFF9B, ofs, &c, 1);
}

/* a file static */
static void display_texture(int no, LtProperty *e)
{
    SprRect ofs;
    SprRect box;

    union { /* field names derived */
        int pt[2];
        SprCol col;
    } u;

    int sel;
    int i;

    ofs.x = (e->texU << 4) + 8;
    ofs.y = (e->texV << 4) + 8;
    ofs.w = e->texW << 4;
    ofs.h = e->texH << 4;

    box.w = e->dispW << 4;
    box.h = e->dispH << 3;
    if (box.w == 0) {
        box.w = ofs.w;
    }
    if (box.h == 0) {
        box.h = ofs.h >> 1;
    }
    if (e->centerX != 0) {
        box.x = (10240 - box.w) / 2 - 5120;
    } else {
        box.x = (e->dispX - 320) * 16;
    }
    box.y = (e->dispY - 113) * 16;

    sel = (e == &LT_PROP(LT_LAYOUT(no).curItem));
    if (sel && lt_item_select_disable == 0 && fadeState == 2 && e->selectable == 0) {
        SprCol pcol;

        memset(&pcol, 0, sizeof(pcol));
        pcol.a = 127;
        gif_StartPacketPri(11);
        gif_SetZTest(0);
        for (i = 0; i < box.w * box.h / 1200; i++) {
            u.pt[0] = box.x + rand() % box.w;
            u.pt[1] = box.y + rand() % box.h;
            pcol.a = rand() % 127;
            gif_PointOffset(u.pt, 0x800000, &pcol, 1);
        }
        gif_EndPacket();
    }
    if (e->masked == 0) {
        int flag;

        flag = (e->upItem >= 0 || e->downItem >= 0 || e->leftItem >= 0 || e->rightItem >= 0 ||
                e->right >= 0 || e->left >= 0 || e->down >= 0 || e->up >= 0);
#ifdef ICO_HOST
        /* PC port (6B): a port row has no texture; its label is drawn where
           the sprite would be, with the same colour */
        ltHostTextRow = lt_ext_IsPortProp(e) ? e : 0;
        if (ltHostTextRow == 0)
#endif
            tex_TransTexture(e->texNo, 11);

        gif_StartPacketPri(11);
        gif_SetZTest(0);
        gif_SetZWrite(0);
        gif_SetAlpha(1, 7, 0);
        box.y = box.y + 4;
        box.x = box.x + 4;
        gif_SetGsReg(20, 96);
        box.h = box.h - 16;
        ofs.h = ofs.h - 16;
        box.w = box.w - 16;
        ofs.w = ofs.w - 16;
        if (e->fade_cancel == 0) {
            u.col = *(SprCol *)ltCursorColor;
        } else {
            u.col = *(SprCol *)&ltHighlightColor;
        }
        u.col.r = ~GlobalStageSetting.reductionCol[0];
        u.col.g = ~GlobalStageSetting.reductionCol[1];
        u.col.b = ~GlobalStageSetting.reductionCol[2];
        if (u.col.r < 120 || u.col.g < 120 || u.col.b < 120) {
            if (u.col.r >= 17) {
                u.col.r = u.col.r - 16;
            } else {
                u.col.r = 0;
            }
            if (u.col.g >= 17) {
                u.col.g = u.col.g - 16;
            } else {
                u.col.g = 0;
            }
            if (u.col.b >= 17) {
                u.col.b = u.col.b - 16;
            } else {
                u.col.b = 0;
            }
        }
        if (LT_LAYOUT(no).curItem != e->ownerItem) {
            if (e->selectable != 0 && sel == 0 && (flag != 0 || e->ownerItem >= 0)) {
                u.col.r = u.col.r * 0.5f;
                u.col.g = u.col.g * 0.5f;
                u.col.b = u.col.b * 0.5f;
            }
        }
#ifdef ICO_HOST
        if (ltHostTextRow != 0) {
            lt_ext_DrawRow(e, (const int *)&box, (const unsigned char *)&u.col, 0);
        } else
#endif
            gif_SpriteSensitiveOffset(&box, 0xFFFFFF9B, &ofs, &u.col, 1);

        if (e->selectable != 0 && sel != 0 && fadeState == 8) {
            float t = (float)fadeCount / (float)fadeLength;

            lt_glow_sprite(&box, &ofs, 80, 80, 80, t, 55, 50);
            lt_glow_sprite(&box, &ofs, 30, 30, 30, t, 110, 100);
        } else if (e->selectable != 0 && sel != 0 && glowOn != 0) {
            lt_glow_sprite(&box, &ofs, 54, 80, 115, (float)glowCount / (float)glowLength, 32, 32);
        }
#ifdef ICO_HOST
        ltHostTextRow = 0;
#endif
        gif_SetZWrite(1);
        gif_EndPacket();
    }
}

static inline void lt_draw_primary_sprite(SprCol *col) /* derived name */
{
    SprRect r;

    gif_StartPacketPri(11);
    gif_SetZTest(0);
    gif_SetZWrite(0);
    gif_SetAlpha(1, 7, 0);
    r = primarySpriteRect;
    gif_SpriteSensitive(&r, 0xFFFFFFFF, (void *)0, col, 1);
    gif_SetZWrite(1);
    gif_SetZTest(1);
    gif_EndPacket();
}

static void display_primary_texture_layout(int no, int sel)
{
    SprCol col;
    LtProp *p = &LT_LAYOUT(no);
    int m;
    int flag;

    col.r = (int)(p->colR * 255.0f);
    col.g = (int)(p->colG * 255.0f);
    col.b = (int)(p->colB * 255.0f);
    col.a = (int)(p->colA * 127.0f);
    lt_draw_primary_sprite(&col);
    if (p->proc != 0 && (fadeState == 1 || fadeState == 2)) {
        if (p->curItem >= 0) {
            m = LT_PROP(p->curItem).selectMode;
        } else {
            m = 0;
        }
        sel = p->proc(ltSelectFlag, sel);
        if (sel != -1) {
            flag = 0;
            if ((pad[0].flags & 0x40) != 0) {
                flag = m == 1;
            }
            if ((fadeState == 2 && sel != current_layout_id) || sel == 62) {
                nextLayout = sel;
                display_texture_fade_cancel_chk(current_layout_id, sel);
                if (fadeType == 1) {
                    fadeState = 5;
                } else {
                    nextFadeState = 3;
                    fadeState = flag ? 7 : 3;
                }
            }
        } else if ((pad[0].flags & 0x40) != 0) {
            if (m == 2) {
                nextFadeState = m;
                fadeState = 7;
            }
        }
        ltSelectFlag = 0;
    }
    texture_fading(p);
    lt_draw_layout(no);
}

void exec_layout_texture(void)
{
    int n = 0;
    int list[16];
    int ret = -1;
    LtProp *p;
    int v;
    int j;
    int k;

    if (frame_count - selectFrame == 0 || frame_count - selectFrame == 1) {
        pad[0].flags = 0;
    }
    p = &LT_LAYOUT(current_layout_id);
    for (;;) {
        for (j = p->first; j < p->last; j++) {
            LtProperty *e = &LT_PROP(j);

            e->masked = e->defaultMask;
        }
        if (p->link >= 0) {
            list[n++] = p->link;
            p = &LT_LAYOUT(p->link);
        } else {
            break;
        }
    }
    list[n] = -1;
    for (n--; n != -1; n--) {
        p = &LT_LAYOUT(list[n]);
        v = p->curItem;
        ltCurrentItem = v;
        if (p->proc != 0 && (fadeState == 1 || fadeState == 2)) {
            ret = p->proc(p->procFirst, ret);
            p->procFirst = 0;
            v = p->curItem;
        } else {
            ret = -1;
        }
        if (v >= 0 && lt_item_select_disable == 0) {
            default_item_select(list[n]);
        }
    }
    p = &LT_LAYOUT(current_layout_id);
    ltCurrentItem = p->curItem;
    display_primary_texture_layout(current_layout_id, ret);
    if (p->curItem >= 0 && lt_item_select_disable == 0) {
        default_item_select(current_layout_id);
    }
    for (k = 0; list[k] >= 0; k++) {
        lt_draw_layout(list[k]);
    }
    lt_item_select_disable = 0;
}

/* init_textures_of_specified_property is a file static, as is
   ico2/common/src/kanban's function of the same name */
/* texProperty's texNo column, &texProperty[0].texNo */
#ifdef ICO_HOST
#define D_0030D014 ((char *)&texProperty[0].texNo)
#else

extern char D_0030D014[];

#endif

/* sce/'s string.h does not declare it */
extern char *strtok(char *s, const char *sep);

static inline char *lt_texture_base_name(char *src) /* derived name */
{
#ifdef ICO_HOST
    /* the result points into it, so on the host it outlives the call */
    static char buf[256];
#else
    char buf[256];
#endif
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

static inline int lt_texture_no_of_property(int idx) /* derived name */
{
    int n;
    char *src;
    char *name;
    int no;

    n = texProperty[idx].texFileNo;
    src = texFile[n].path;
    name = lt_texture_base_name(src);

    no = tex_GetTextureNo(name);

    if (no < 0) {
        debug_StdPrintfDummy("no texture loaded.(%s)\n", src);
        debug_assert(__FILE__, 1287);
        __assert(__FILE__, 1287, "0");
    }
    return no;
}

static void init_textures_of_specified_property(int first, int last)
{
    int i;
    int no;

    for (i = first; i < last; i++) {
        no = lt_texture_no_of_property(i);
#ifdef ICO_HOST
        /* the EE's stride and texData-before-texNo offset are 32-bit only */
        texProperty[i].texNo = no;
        texProperty[i].texData = tex_GetTextureData(no);
        tex_SetSamplingType(texProperty[i].texData, 1, 1);
#else
        *(int *)(D_0030D014 + i * 0x70) = no;
        *(void **)(D_0030D014 + i * 0x70 - 4) = tex_GetTextureData(no);
        tex_SetSamplingType(*(void **)(D_0030D014 + i * 0x70 - 4), 1, 1);
#endif
    }
}

static inline void lt_init_stage_textures(int stage) /* derived name */
{
    int i = stageData[stage].layoutFirst;
    int last = stageData[stage].layoutLast;

    for (; i < last; i++) {
        init_textures_of_specified_property(texLayout[i].first, texLayout[i].last);
    }
    ltCurrentItem = LT_LAYOUT(current_layout_id).defaultItem;
}

void init_layout_texture(int stage)
{
    fadeCallback = 0;
    if (stage == 1) {
        gflagInit();
        if (layout_boot_flag == 0) {
            current_layout_id = 7;
        } else if (mpegPlayReturnStage == stage) {
            mpegPlayReturnStage = 0;
            if (stage_after_skipping_demo == 0xFFFFFFFE) {
                title_demo_mode = title_demo_mode ^ 1;
                current_layout_id = 13;
            } else if (stage_after_skipping_demo == 0xFFFFFFFF) {
                current_layout_id = 10;
                title_demo_mode = title_demo_mode ^ 1;
            } else {
                current_layout_id = 13;
            }
        } else {
            current_layout_id = 13;
        }
    } else {
        current_layout_id = 54;
    }
    lt_init_stage_textures(stage);
    LT_LAYOUT(current_layout_id).curItem = LT_LAYOUT(current_layout_id).defaultItem;
    ltSelectFlag = 1;
    fadeState = 0;
    lt_reset_property_chain(current_layout_id);
}

inline void lt_switch_layout(int no)
{
    if ((fadeState == 2 && no != current_layout_id) || no == 62) {
        nextLayout = no;
        display_texture_fade_cancel_chk(current_layout_id, no);
        if (fadeType == 1) {
            fadeState = 5;
        } else {
            nextFadeState = 3;
            fadeState = 3;
        }
    }
}

inline int lt_current_property_item(void)
{
    return ltCurrentItem;
}

inline int lt_link_layout(int dir)
{
    switch (dir) {
    case 0:
        return LT_PROP(lt_current_property_item()).right;
    case 1:
        return LT_PROP(lt_current_property_item()).left;
    case 2:
        return LT_PROP(lt_current_property_item()).down;
    case 3:
        return LT_PROP(lt_current_property_item()).up;
    }
    return -1;
}

inline int lt_prev_layout(int stage)
{
    current_layout_id = current_layout_id - 1;
    if (current_layout_id < stageData[stage].layoutFirst) {
        current_layout_id = stageData[stage].layoutLast - 1;
    }
    lt_switch_layout(current_layout_id);
    return current_layout_id;
}

inline int lt_next_layout(int stage)
{
    current_layout_id = current_layout_id + 1;
    if (current_layout_id >= stageData[stage].layoutLast) {
        current_layout_id = stageData[stage].layoutFirst;
    }
    lt_switch_layout(current_layout_id);
    return current_layout_id;
}

inline void lt_mask_property(int idx, int flag)
{
    LtProperty *p = &LT_PROP(idx);
    p->masked = flag & 1;
}

inline void lt_default_mask_property(int idx, int flag)
{
    LtProperty *p = &LT_PROP(idx);
    p->defaultMask = flag & 1;
}

inline int lt_fade_status(void)
{
    return fadeState;
}

inline void lt_set_item_select_func(ICO_WORD_PTR(LtSelectFn) val)
{
    fadeCallback = val;
}

inline void lt_set_fade_mode(int val)
{
    fadeType = val;
}
