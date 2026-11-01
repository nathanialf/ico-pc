/*
 * port/ui/settings_backup.c
 *
 * Settings > Extras > Back up your data and Restore from a backup
 * (settings_backup.h) over the host settings.h describes
 * (port/platform/backup_host.c in the window build).  Back up your data
 * asks the host for its save dialog at once.  Restore from a backup asks
 * twice before anything is chosen: the first press says in a popup what is
 * replaced, and a second press within RESTORE_CONFIRM_SECONDS opens the
 * file dialog; once the host has read the chosen backup, a popup says
 * which version made it, when and with which discs, and a third press
 * restores it (moving the cursor off the row, or RESTORE_ASK_SECONDS,
 * drops it).  The end popup of a job the rows started comes from
 * ui_settings_vsync, on whatever screen is up.
 */
#include "settings.h"
#include "settings_backup.h"

#include <stdio.h>
#include <string.h>

#include "popup.h"
#include "strings.h"

#define RESTORE_CONFIRM_SECONDS 8
#define RESTORE_ASK_SECONDS 60

static UiBackupHost s_host;
static int s_hostSet;
static int s_pending;    /* a job the rows started has not ended: its popup to come */
static int s_confirm;    /* Restore pressed once: the second press opens the dialog */
static int s_confirmAge; /* vsyncs since */
static int s_askShown;   /* the read backup's popup was shown */
static int s_askAge;     /* vsyncs it has waited */

void ui_settings_set_backup_host(const UiBackupHost *host)
{
    s_hostSet = host != NULL && host->begin != NULL && host->poll != NULL &&
                host->proceed != NULL && host->cancel != NULL && host->pump != NULL;
    if (s_hostSet) {
        s_host = *host;
    }
    s_pending = s_confirm = s_askShown = 0;
}

int ui__backup_hosted(void)
{
    return s_hostSet;
}

static int poll(UiBackupStatus *st)
{
    memset(st, 0, sizeof(*st));
    if (!s_hostSet) {
        return UI_BACKUP_IDLE;
    }
    st->state = s_host.poll(st);
    st->name[sizeof(st->name) - 1] = '\0';
    st->version[sizeof(st->version) - 1] = '\0';
    st->date[sizeof(st->date) - 1] = '\0';
    st->discs[sizeof(st->discs) - 1] = '\0';
    return st->state;
}

/* a job under way: a dialog, the work, a backup read waiting, or a restore
   whose run is ending */
static int underWay(const UiBackupStatus *st)
{
    return st->state == UI_BACKUP_CHOOSING || st->state == UI_BACKUP_WORKING ||
           st->state == UI_BACKUP_ASK ||
           (st->state == UI_BACKUP_DONE && st->job == UI_BACKUP_JOB_RESTORE);
}

static void dropAsk(const char *why)
{
    UiBackupStatus st;
    if (poll(&st) == UI_BACKUP_ASK) {
        s_host.cancel();
        fprintf(stderr, "extras: restore from a backup: not restored (%s)\n", why);
    }
}

int ui__backup_label(int opt, int onCursor)
{
    UiBackupStatus st;
    if (opt != UI_OPT_EXTRAS_RESTORE) {
        return UI_STR_EXTRAS_BACKUP;
    }
    if (!onCursor) {
        s_confirm = 0;
        dropAsk("the cursor moved off");
    }
    if (poll(&st) == UI_BACKUP_ASK && st.job == UI_BACKUP_JOB_RESTORE) {
        return UI_STR_RESTORE_THIS;
    }
    return s_confirm ? UI_STR_RESTORE_AGAIN : UI_STR_EXTRAS_RESTORE;
}

int ui__backup_note(int opt, char *text, unsigned size)
{
    UiBackupStatus st;
    const int state = poll(&st);
    const int mine =
        st.job == (opt == UI_OPT_EXTRAS_RESTORE ? UI_BACKUP_JOB_RESTORE : UI_BACKUP_JOB_SAVE);
    const int v[2] = {st.done, st.total};
    text[0] = '\0';
    if (opt == UI_OPT_EXTRAS_BACKUP) {
        if (mine && state == UI_BACKUP_CHOOSING) {
            return UI_STR_BACKUP_CHOOSING;
        }
        if (mine && state == UI_BACKUP_WORKING) {
            ui__settings_fmt(UI_STR_BACKUP_PROGRESS, "dd", v, NULL, text, size);
            return UI_STR_BACKUP_PROGRESS;
        }
        return UI_STR_BACKUP_NOTE;
    }
    if (mine && state == UI_BACKUP_CHOOSING) {
        return UI_STR_RESTORE_CHOOSING;
    }
    if (mine && state == UI_BACKUP_WORKING) {
        if (st.done > 0 && st.total > 0) {
            ui__settings_fmt(UI_STR_RESTORE_PROGRESS, "dd", v, NULL, text, size);
            return UI_STR_RESTORE_PROGRESS;
        }
        return UI_STR_RESTORE_READING;
    }
    if (mine && state == UI_BACKUP_ASK) {
        ui__settings_fmt(UI_STR_RESTORE_ASK_NOTE, "s", NULL, st.version, text, size);
        return UI_STR_RESTORE_ASK_NOTE;
    }
    if (mine && state == UI_BACKUP_DONE) {
        return st.restarts ? UI_STR_RESTORE_DONE_RESTART : UI_STR_RESTORE_DONE_CLOSE;
    }
    return UI_STR_RESTORE_NOTE;
}

/* the read backup's popup: its version, when it was made, its discs */
static void askPopup(const UiBackupStatus *st)
{
    char body[UI_POPUP_TEXT], line[160];
    size_t n = 0;

    const struct {
        int id;
        const char *str;
    } lines[] = {
        {UI_STR_RESTORE_VERSION, st->version},
        {UI_STR_RESTORE_SAVED, st->date},
        {UI_STR_RESTORE_DISCS, st->discs},
    };

    body[0] = '\0';
    for (unsigned i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        if (lines[i].str[0] == '\0') {
            continue;
        }
        ui__settings_fmt(lines[i].id, "s", NULL, lines[i].str, line, sizeof(line));
        n += (size_t)snprintf(body + n, sizeof(body) - n, "%s%s", n > 0 ? "\n" : "", line);
        if (n >= sizeof(body)) {
            n = sizeof(body) - 1;
        }
    }
    if (st->otherDisc) {
        n += (size_t)snprintf(body + n, sizeof(body) - n, "%s%s", n > 0 ? "\n" : "",
                              ui_str(UI_STR_RESTORE_OTHER_DISC));
        if (n >= sizeof(body)) {
            n = sizeof(body) - 1;
        }
    }
    snprintf(body + n, sizeof(body) - n, "%s%s", n > 0 ? "\n" : "", ui_str(UI_STR_RESTORE_PRESS));
    (void)ui_popup_push(ui_str(UI_STR_RESTORE_ASK_TITLE), body);
}

static const int kWhyStr[] = {
    [UI_BACKUP_WHY_NONE] = UI_STR_BACKUP_WHY_WRITE,
    [UI_BACKUP_WHY_BUSY] = UI_STR_BACKUP_WHY_BUSY,
    [UI_BACKUP_WHY_NOTHING] = UI_STR_BACKUP_WHY_NOTHING,
    [UI_BACKUP_WHY_WRITE] = UI_STR_BACKUP_WHY_WRITE,
    [UI_BACKUP_WHY_READ] = UI_STR_BACKUP_WHY_READ,
    [UI_BACKUP_WHY_NOT_BACKUP] = UI_STR_BACKUP_WHY_NOT_BACKUP,
    [UI_BACKUP_WHY_NEWER] = UI_STR_BACKUP_WHY_NEWER,
    [UI_BACKUP_WHY_DAMAGED] = UI_STR_BACKUP_WHY_DAMAGED,
    [UI_BACKUP_WHY_UNSAFE] = UI_STR_BACKUP_WHY_UNSAFE,
    [UI_BACKUP_WHY_TOO_BIG] = UI_STR_BACKUP_WHY_TOO_BIG,
    [UI_BACKUP_WHY_NO_DIALOG] = UI_STR_BACKUP_WHY_NO_DIALOG,
    [UI_BACKUP_WHY_NO_SPACE] = UI_STR_BACKUP_WHY_NO_SPACE,
};

/* the end popup of a job that ended in st->state (a closed dialog has
   none) */
static void endPopup(const UiBackupStatus *st)
{
    char body[UI_POPUP_TEXT];
    const int restore = st->job == UI_BACKUP_JOB_RESTORE;
    if (st->state == UI_BACKUP_CANCELLED) {
        return;
    }
    if (st->state == UI_BACKUP_DONE) {
        if (restore) {
            (void)ui_popup_push(
                ui_str(UI_STR_RESTORE_DONE),
                ui_str(st->restarts ? UI_STR_RESTORE_DONE_RESTART : UI_STR_RESTORE_DONE_CLOSE));
        } else {
            const int v[1] = {st->done};
            (void)ui_popup_push(
                ui_str(UI_STR_BACKUP_DONE),
                ui__settings_fmt(UI_STR_BACKUP_DONE_BODY, "ds", v, st->name, body, sizeof(body)));
        }
        return;
    }
    const int why = st->why >= 0 && st->why < (int)(sizeof(kWhyStr) / sizeof(kWhyStr[0]))
                        ? kWhyStr[st->why]
                        : UI_STR_BACKUP_WHY_WRITE;
    (void)ui_popup_push(ui_str(restore ? UI_STR_RESTORE_FAILED : UI_STR_BACKUP_FAILED),
                        ui_str((UiStrId)why));
}

int ui__backup_press(int opt)
{
    UiBackupStatus st;
    poll(&st);
    if (!s_hostSet) {
        return -1;
    }
    if (opt == UI_OPT_EXTRAS_RESTORE && st.state == UI_BACKUP_ASK &&
        st.job == UI_BACKUP_JOB_RESTORE) {
        fprintf(stderr, "extras: restore from a backup: restore %s\n", st.name);
        s_host.proceed();
        return 1;
    }
    if (underWay(&st)) {
        fprintf(stderr, "extras: %s: one is under way (state %d)\n",
                opt == UI_OPT_EXTRAS_RESTORE ? "restore from a backup" : "back up your data",
                st.state);
        return -1;
    }
    if (opt == UI_OPT_EXTRAS_RESTORE && !s_confirm) {
        /* the first press: what a restore replaces, and the second press */
        s_confirm = 1;
        s_confirmAge = 0;
        fprintf(stderr, "extras: restore from a backup: asked to press again\n");
        (void)ui_popup_push(ui_str(UI_STR_RESTORE_CONFIRM_TITLE),
                            ui_str(UI_STR_RESTORE_CONFIRM_BODY));
        return 0;
    }
    s_confirm = 0;
    const int job = opt == UI_OPT_EXTRAS_RESTORE ? UI_BACKUP_JOB_RESTORE : UI_BACKUP_JOB_SAVE;
    if (s_host.begin(job) != 0) {
        fprintf(stderr, "extras: %s: the host is busy\n",
                job == UI_BACKUP_JOB_RESTORE ? "restore from a backup" : "back up your data");
        return -1;
    }
    s_pending = 1;
    s_askShown = 0;
    fprintf(stderr, "extras: %s: started\n",
            job == UI_BACKUP_JOB_RESTORE ? "restore from a backup" : "back up your data");
    return 1;
}

void ui__backup_vsync(void)
{
    UiBackupStatus st;
    const int hz = ui_popup_vsync_hz();
    if (!s_hostSet) {
        return;
    }
    s_host.pump();
    if (s_confirm && ++s_confirmAge > RESTORE_CONFIRM_SECONDS * hz) {
        s_confirm = 0;
    }
    const int state = poll(&st);
    if (state == UI_BACKUP_ASK) {
        if (!s_askShown) {
            s_askShown = 1;
            s_askAge = 0;
            ui_settings_apply_language();
            askPopup(&st);
        } else if (++s_askAge > RESTORE_ASK_SECONDS * hz) {
            dropAsk("no press within a minute");
        }
        return;
    }
    s_askShown = 0;
    if (s_pending &&
        (state == UI_BACKUP_DONE || state == UI_BACKUP_FAILED || state == UI_BACKUP_CANCELLED)) {
        s_pending = 0;
        fprintf(stderr, "extras: %s ended: state %d, %d of %d files, reason %d\n",
                st.job == UI_BACKUP_JOB_RESTORE ? "restore from a backup" : "back up your data",
                state, st.done, st.total, st.why);
        ui_settings_apply_language();
        endPopup(&st);
    }
}
