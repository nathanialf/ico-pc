/*
 * port/platform/backup_host.h
 *
 * Options > Extras > Back up your data and Restore from a backup in the
 * window build (backup_host.c): the rows' host (port/ui/settings.h
 * UiBackupHost) over backup.h, with the system's file dialogs (SDL's
 * save and open dialogs on the desktop; on Android the app's own document
 * screens, host_android.h ico_android_create_file and
 * ico_android_pick_file), the work on a thread of its own, and the end of
 * the run after a restore. Not on iOS (no rows there).
 */
#ifndef ICO_PLATFORM_BACKUP_HOST_H
#define ICO_PLATFORM_BACKUP_HOST_H

/* Sets the host (ui_settings_set_backup_host); main_host.c, with the
   Save states host, before the menus are built. Nothing on iOS. */
void ico_backup_host_install(void);

#endif /* ICO_PLATFORM_BACKUP_HOST_H */
