/*
 * ico2/seki/include/Texture.h
 *
 * The declarations of what Texture.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef TEXTURE_H
#define TEXTURE_H

/* the 0x40-byte block the ICO tools append to the TIM2 header, recognised by
 * its "ICO" magic and copied whole into the first 0x40 bytes of the record's
 * TexExt at 0x268. The two ints at 0x28 and 0x2C are the mag and min filter
 * terms tex_UpdateMipMapLevel reads back as x290 and x294, and the two shorts
 * at 0x3C and 0x3E the terms it reads back as x2A4 and x2A6. */
typedef struct Tim2Ext { /* field names derived */
    char magic[4];
    /* the U and V scroll speeds, and behind them the U and V amplitudes the
     * sine animation multiplies its sample by */
    float scrlU;
    float scrlV;
    float ampU;
    float ampV;
    /* the CLUT scroll's first and last entry */
    int csBgn;
    int csEnd;
    /* the two enables tex_textureAnimation tests before it scrolls the CLUT */
    int csSpd;
    int csStp;
    /* SHINE */
    int shine;
    /* SMPMAG and SMPMIN */
    int smpMag;
    int smpMin;
    /* TEXFNC, ALPTST and ALPFAI */
    int texFnc;
    int alpTst;
    int alpFai;
    /* MIPMAPK and MIPMAPL */
    short mipmapK;
    short mipmapL;
} Tim2Ext; /* derived name */

/* the animation record at 0x268 of the texture record. It opens with the
 * 0x40-byte ICO block copied off the TIM2 header and continues with the state
 * the animation keeps between frames. */
typedef struct TexExt { /* field names derived */
    Tim2Ext file;
    int animated; /* set when the TIM2 carries the ICO block; tex_Tool walks the table by it */
    float uLimit; /* the U and V offsets the scroll stops at while limitOn is set */
    float vLimit;
    int limitOn;
    unsigned short frame;     /* the UV animation's frame */
    unsigned short clutFrame; /* the CLUT scroll's frame */
    /* the three CLUT copies tex_initTextureSub allocates for the scroll */
    void *clutA;
    void *clutB;
    void *clutOrg; /* the untouched copy the other two are restored from */
    /* one byte per display list priority: the slot's transfer-done flag */
    char transDone[8];
    unsigned int pad68;
    unsigned char pad6C;
    unsigned short used : 1;
    /* the mipmap level the record is drawn from */
    unsigned short level : 15;
    unsigned short pad6F : 1;
    short partition; /* the allocator partition the record was built in */
    char pad72[6];
} TexExt; /* derived name */

/* the texture record, one per slot of Texture.c's table; the other files
 * hold it only through tex_GetTextureData */
typedef struct TexData TexData; /* derived name */

int tex_AllocVramAuto(int kind, int size);
TexExt *tex_GetTexExtData(int idx);
TexData *tex_GetTextureData(int idx);
char *tex_GetTextureName(int idx);
int tex_GetTextureNo(const char *name);
int tex_GetTextureNum(void);
void tex_Init(void);
int tex_InitTexture(char *name, void *pkt);
int tex_FreeTexture(int id);
int tex_ListTool(void);
int tex_LoadTexturePart(char *name, int area);
void tex_LockHeadTBP(int tbp, int pri);
int tex_RemakeRegistersSampleMin(int arg);
void tex_ResetVramPri(int pri);
void tex_SetSamplingType(TexData *tex, int mag, int min);

void tex_SetUVScroll(const char *name, float u, float v, float su, float sv, float ou, float ov,
                     int limitOn);

int tex_TransTexture(int no, int pri);
void tex_UnlockHeadTBP(int pri);
void tex_ResetVram(void);
void tex_UpdateMipMapLevel(float lv);
int tex_GetTWTH(int size);
short tex_GetVramFreeAddress(int pri);

#ifdef ICO_RD

/* PC port (renderer wave 2, R2b): the rd texture id (RdTex.id) of table
 * entry idx at its current content, decoded into the texture cache on a
 * miss; 0 when the entry has no texture.  For tests and tools. */
unsigned int tex_HostTextureId(int idx);

#endif
#endif /* TEXTURE_H */
