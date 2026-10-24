/*
 * ico2/fumi/include/mcdata.h
 *
 * The declarations of what mcdata.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef MCDATA_H
#define MCDATA_H

struct McMgr;
struct IconFile;

/* PC port: upstream's June 2001 demo arm. The save's icon.sys record (964
   bytes) under the SDK's name sceMcIconSys, its field names spelled after the
   SDK's (derived). The June 2001 demo builds one into the program (the data
   member mc-icon-sys, an extra row of config/data_schema.jun26.txt, which the
   loader fills from that disc's ELF); upstream's code reads it nowhere (the
   demo writes the icon.sys its DATA.DF holds, iconFile's icon/icon.sys). */
typedef struct {                 /* field names derived */
    unsigned char Head[4];       /* 0x0 "PS2D" */
    unsigned short Reserv1;      /* 0x4 */
    unsigned short OffsLF;       /* 0x6 */
    unsigned int Reserv2;        /* 0x8 */
    unsigned int TransRate;      /* 0xC */
    int BgColor[4][4];           /* 0x10 */
    float LightDir[3][4];        /* 0x50 */
    float LightColor[3][4];      /* 0x80 */
    float Ambient[4];            /* 0xB0 */
    unsigned char TitleName[68]; /* 0xC0 */
    unsigned char FnameView[64]; /* 0x104 */
    unsigned char FnameCopy[64]; /* 0x144 */
    unsigned char FnameDel[64];  /* 0x184 */
    unsigned char Reserve3[512]; /* 0x1C4 */
} sceMcIconSys;

int iosMcIconWriteIconsys(struct McMgr *self, const struct IconFile *p);
int iosMcIconWriteIcon(struct McMgr *self, const struct IconFile *p);

#endif /* MCDATA_H */
