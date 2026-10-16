/* libcdvd.a(cdvd006): sceCdReadIOPm. */
#include <eekernel.h>
#include <stdio.h>
#include <sifrpc.h>
#include <libcdvd.h>
#include <libcdvd_internal.h>

typedef struct { /* field names derived */
    int lsn;
    int sectors;
    void *buf;
    unsigned char trycount;
    unsigned char spindlctrl;
    unsigned char datapattern;
    unsigned char pad;
    int *intr_data;
    int *cur_pos;
} CdReadCmd; /* derived name */

/* volatile here; cdvd000 defines it plain and releases it with plain stores */
extern volatile int _sceCd_c_cb_sem;

int sceCdReadIOPm(int lsn, int sectors, void *buf, CdRMode *mode)
{
    CdReadCmd *sd = (CdReadCmd *)_sceCd_ncmdsdata;

    if ((_sceCd_ee_read_mode & 1) == 0) {
        if (sceCdNcmdDiskReady() == 6) {
            return 0;
        }
    }
    if (_sceCd_ncmd_prechk(5) == 0) {
        return 0;
    }
    sd->lsn = lsn;
    sd->sectors = sectors;
    sd->buf = buf;
    sd->trycount = mode->trycount;
    sd->spindlctrl = mode->spindlctrl;
    sd->datapattern = mode->datapattern;
    sd->intr_data = _sceCd_rd_intr_data;
    sd->cur_pos = _sceCd_Read_cur_pos;
    sceSifWriteBackDCache(sd, 24);
    sceCdCbfunc_num = 1;
    _sceCd_c_cb_sem = 1;
    if (sceSifCallRpc(&_sceCd_cd_ncmd, 13, 1, sd, 24, 0, 0, _sceCd_cd_callback,
                      (void *)&sceCdCbfunc_num) < 0) {
        sceCdCbfunc_num = 0;
        _sceCd_c_cb_sem = 0;
        SignalSema(*(volatile int *)&_sceCd_ncmd_semid);
        return 0;
    }
    if (SCE_CD_debug > 0) {
        scePrintf("cdread end\n");
    }
    return 1;
}
