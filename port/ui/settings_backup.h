/*
 * port/ui/settings_backup.h
 *
 * Settings > Extras > Back up your data and Restore from a backup: the two
 * rows' words, Cross on them, the confirmations and the popups
 * (settings_backup.c), for the page settings.c builds.  The public side
 * (the host) is settings.h's ui_settings_set_backup_host.
 */
#ifndef PORT_UI_SETTINGS_BACKUP_H
#define PORT_UI_SETTINGS_BACKUP_H

#ifdef __cplusplus
extern "C" {
#endif

/* a host is set: the rows show (from the title) */
int ui__backup_hosted(void);
/* the row's label now (a UiStrId); opt UI_OPT_EXTRAS_BACKUP or
   UI_OPT_EXTRAS_RESTORE.  onCursor 0 on the Restore row ends its
   confirmations (the cursor moved off: a first press waiting for its
   second, a backup read and waiting to be restored, which is dropped) */
int ui__backup_label(int opt, int onCursor);
/* the row's note: its table string (a UiStrId) with text left "", or the
   words written into text (the progress, the backup read), wrapped by the
   caller */
int ui__backup_note(int opt, char *text, unsigned size);
/* Cross on the row: 1 a job began or went on, 0 the first press of the
   Restore row (a second within a few seconds goes on), -1 nothing (a job
   is under way) */
int ui__backup_press(int opt);
/* one vsync (ui_settings_vsync): the host's pump, the confirmations'
   time, the popups for a backup read and for a job's end */
void ui__backup_vsync(void);

/* settings.c's fmtText: the table string id with the conversions want
   names ('d' the next of v[], 's' str), a translation that does not match
   falling back to the English one */
const char *ui__settings_fmt(int id, const char *want, const int *v, const char *str, char *buf,
                             unsigned size);

#ifdef __cplusplus
}
#endif

#endif /* PORT_UI_SETTINGS_BACKUP_H */
