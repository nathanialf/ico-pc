#include "typedef.h"
#include "obj_manager.h"
#include "debug_exception.h"
#include <assert.h>
#include "memory.h"
#include "ios.h"

/* Two static helpers, one sending the mail and returning its index or -1,
 * the other asserting the actor has a work block and returning its
 * additional-data table.  InitMailAdditionalData calls
 * ClearMailAdditionalData, which is defined after it. */

/* the entries a table holds; the name and current_count are the assert's */
#define MAIL_ADDITIONAL_DATA_MAX 10

typedef struct MailAddEntry { /* field names derived */
    /* 0x0 */ int mail;
    /* 0x4 */ void *data;
} MailAddEntry; /* derived name */

typedef struct MailAdditionalData { /* field names derived */
    /* 0x00 */ int current_count;
    /* 0x04 */ MailAddEntry e[MAIL_ADDITIONAL_DATA_MAX];
} MailAdditionalData; /* derived name */

static inline int sendMailAndGetIndex(GObj *gop, int msg, void *sender) /* derived name */
{
    if (iosOmSendMail(gop, msg, sender) < 0) {
        return -1;
    }
    return gop->mailBox.num - 1;
}

static inline MailAdditionalData *getMailAdditionalDataTable(GObj *gop) /* derived name */
{
    if (gop->act == 0) {
        debug_assert("src/mail-add-data.c", 71);
        __assert("src/mail-add-data.c", 71, "GOBJ_VAL(gop)");
    }
    /* Act's 0x684 holds this table's address, an int in typedef.h's Act */
    return GOBJ_ACT(gop)->mailAddData;
}

#include "mail-add-data.h"

inline int ActSendMail_WithAdditionalData(GObj *gop, int msg, void *sender, void *data)
{
    int idx;
    MailAdditionalData *mad_all;

    idx = sendMailAndGetIndex(gop, msg, sender);
    if (idx < 0) {
        return -1;
    }
    mad_all = getMailAdditionalDataTable(gop);
    if (mad_all->current_count >= MAIL_ADDITIONAL_DATA_MAX) {
        debug_assert("src/mail-add-data.c", 95);
        __assert("src/mail-add-data.c", 95, "mad_all->current_count<MAIL_ADDITIONAL_DATA_MAX");
    }
    mad_all->e[mad_all->current_count].mail = idx;
    mad_all->e[mad_all->current_count].data = data;
    mad_all->current_count++;
    return 0;
}

inline void *GetMailAdditionalData(GObj *gop, int mail)
{
    MailAdditionalData *p;
    int i;

    p = getMailAdditionalDataTable(gop);
    for (i = 0; i < p->current_count; i++) {
        MailAddEntry *e = &p->e[i];
        if (e->mail == mail) {
            return e->data;
        }
    }
    return 0;
}

void InitMailAdditionalData(GObj *gop, struct MailAdditionalData *table)
{
    /* The EE keeps the table in the first 84 bytes of the enemy work the
       caller passes, which holds the table's 4-byte data pointers.  The host's
       entries are 16 bytes, so the table would overrun that record: it gets
       its own block, from the partition (and lifetime) the enemy work has. */
    table = iosMallocDebug(ios_partition_seki, sizeof(MailAdditionalData), __FILE__, 79);
    GOBJ_ACT(gop)->mailAddData = table;
    ClearMailAdditionalData(gop);
}

inline void ClearMailAdditionalData(GObj *gop)
{
    MailAdditionalData *p;

    p = getMailAdditionalDataTable(gop);
    p->current_count = 0;
}
