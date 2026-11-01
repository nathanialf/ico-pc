/* settings_backup_test.c: Options > Extras > Back up your data and Restore
 * from a backup (settings_backup.c, ui_settings_set_backup_host), over the
 * shared fixture (settings_fixture.h) with a fake UiBackupHost:
 * - no host: both rows hidden; a host on the pause menu: hidden; on the
 *   title: shown after Export textures and before the pop-ups, with their
 *   words and notes, Down and Up between them, the page fits;
 * - Back up your data: Cross asks the host once for the save dialog; the
 *   note says to choose a place, then the progress; ui_settings_vsync
 *   calls the host's pump and shows the done popup once (the files and the
 *   file's name), the failed popup with its reason, nothing for a closed
 *   dialog; a press while one runs asks nothing;
 * - Restore from a backup: the first Cross asks nothing of the host and
 *   shows what a restore replaces; the label asks for a second press,
 *   which opens the dialog; the first press runs out after a few seconds
 *   and when the cursor moves off; the backup read: its popup (version,
 *   date, discs, made with another disc) once, the label and the note ask
 *   for the press that restores it (proceed), the progress, the done
 *   popup (the game restarts, or closes); the read backup dropped
 *   (cancel) when the cursor moves off and after a minute; a refused one's
 *   reason; nothing more is asked while the run ends;
 * - the rows' strings translated in every language;
 * - the other discs (argv[2]): the rows on their numbers and their confirm
 *   button.
 * CPU only.  Exit 0 on success, 1 on a failure. */
#include "settings_fixture.h"
#include "popup.h"

static UiBackupStatus s_st;
static int s_begins, s_lastJob, s_proceeds, s_cancels, s_pumps, s_beginAnswer;

static int fakeBegin(int job)
{
    s_begins++;
    s_lastJob = job;
    if (s_beginAnswer == 0) {
        memset(&s_st, 0, sizeof(s_st));
        s_st.job = job;
        s_st.state = UI_BACKUP_CHOOSING;
    }
    return s_beginAnswer;
}

static int fakePoll(UiBackupStatus *out)
{
    *out = s_st;
    return s_st.state;
}

static void fakeProceed(void)
{
    s_proceeds++;
    s_st.state = UI_BACKUP_WORKING;
}

static void fakeCancel(void)
{
    s_cancels++;
    s_st.state = UI_BACKUP_CANCELLED;
}

static void fakePump(void)
{
    s_pumps++;
}

static const UiBackupHost kHost = {fakeBegin, fakePoll, fakeProceed, fakeCancel, fakePump};

static void resetHost(void)
{
    memset(&s_st, 0, sizeof(s_st));
    s_begins = s_lastJob = s_proceeds = s_cancels = s_pumps = s_beginAnswer = 0;
    ui_settings_set_backup_host(&kHost);
    ui_popup_reset();
}

static int shown(UiSettingsOpt opt)
{
    const int row = ui_settings_row_of(UI_PAGE_EXTRAS, opt);
    return row >= 0 && ui_settings_row_shown(row);
}

static const char *labelOf(UiSettingsOpt opt)
{
    return lt_ext_row_text(ui_settings_row_of(UI_PAGE_EXTRAS, opt));
}

static int popupIs(const char *title, const char *body)
{
    return ui_popup_active() && strcmp(ui_popup_title(), title) == 0 &&
           (body == NULL || strcmp(ui_popup_body(), body) == 0);
}

/* the title's Extras page with the cursor on opt */
static int onRow(UiSettingsOpt opt)
{
    const int mainL = enterMain(1);
    const int exL = openPage(mainL, -1, UI_PAGE_EXTRAS);
    lt_ext_layout(exL)->curItem = ui_settings_row_of(UI_PAGE_EXTRAS, opt);
    frame(0);
    return exL;
}

static void testRows(void)
{
    int mainL, exL;
    resetHost();
    ui_settings_set_backup_host(NULL);
    mainL = enterMain(1);
    exL = openPage(mainL, -1, UI_PAGE_EXTRAS);
    CHECK(ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_BACKUP) >= 0 &&
              !shown(UI_OPT_EXTRAS_BACKUP) && !shown(UI_OPT_EXTRAS_RESTORE),
          "no host: both rows hidden");
    resetHost();
    mainL = enterMain(0);
    exL = openPage(mainL, -1, UI_PAGE_EXTRAS);
    CHECK(!shown(UI_OPT_EXTRAS_BACKUP) && !shown(UI_OPT_EXTRAS_RESTORE),
          "the pause menu: both rows hidden");
    mainL = enterMain(1);
    exL = openPage(mainL, -1, UI_PAGE_EXTRAS);
    CHECK(shown(UI_OPT_EXTRAS_BACKUP) && shown(UI_OPT_EXTRAS_RESTORE),
          "the title: both rows shown");
    {
        int labels[24], opts[24];
        const int n = ui_settings_page_rows(UI_PAGE_EXTRAS, labels, opts, NULL, 24);
        int at = -1;
        for (int i = 0; i < n; i++) {
            if (opts[i] == UI_OPT_EXTRAS_EXPORT_TEXTURES) {
                at = i;
            }
        }
        CHECK(n == 11 && at == 6 && opts[7] == UI_OPT_EXTRAS_BACKUP &&
                  opts[8] == UI_OPT_EXTRAS_RESTORE && opts[9] == UI_OPT_ACH_POPUPS &&
                  opts[10] == UI_OPT_BACK,
              "eleven rows: Export textures, Back up, Restore, the pop-ups, Back (%d)", n);
    }
    CHECK(strcmp(labelOf(UI_OPT_EXTRAS_BACKUP), "Back up your data") == 0 &&
              strcmp(labelOf(UI_OPT_EXTRAS_RESTORE), "Restore from a backup") == 0,
          "the rows' words \"%s\", \"%s\"", labelOf(UI_OPT_EXTRAS_BACKUP),
          labelOf(UI_OPT_EXTRAS_RESTORE));
    lt_ext_layout(exL)->curItem = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_BACKUP);
    frame(0);
    {
        const int note = noteStarting(UI_PAGE_EXTRAS, "Saves your memory card");
        CHECK(note >= 0 && lt_ext_prop(note)->masked == 0, "Back up: its note");
    }
    press(LT_PAD_DOWN);
    CHECK(lt_ext_layout(exL)->curItem == ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_RESTORE),
          "Down from Back up lands on Restore");
    {
        const int note = noteStarting(UI_PAGE_EXTRAS, "Puts back the memory card");
        CHECK(note >= 0 && lt_ext_prop(note)->masked == 0, "Restore: its note");
    }
    press(LT_PAD_UP);
    CHECK(lt_ext_layout(exL)->curItem == ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_BACKUP),
          "Up from Restore lands on Back up");
    checkPageFits(UI_PAGE_EXTRAS, "Extras with the backup rows");
}

static void testBackup(void)
{
    resetHost();
    onRow(UI_OPT_EXTRAS_BACKUP);
    press(lt_ext_confirm_button());
    CHECK(s_begins == 1 && s_lastJob == UI_BACKUP_JOB_SAVE && !ui_popup_active(),
          "Back up: Cross asked for the save dialog once (%d, job %d)", s_begins, s_lastJob);
    frame(0);
    CHECK(noteStarting(UI_PAGE_EXTRAS, "Choose where to save the backup.") >= 0,
          "Back up: the note while the dialog is open");
    s_st.state = UI_BACKUP_WORKING;
    s_st.done = 3;
    s_st.total = 10;
    frame(0);
    CHECK(noteStarting(UI_PAGE_EXTRAS, "Backing up: 3 of 10 files") >= 0, "Back up: the progress");
    press(lt_ext_confirm_button());
    CHECK(s_begins == 1, "Back up: a press while it runs asks nothing");
    ui_settings_vsync();
    CHECK(s_pumps >= 1 && !ui_popup_active(), "the vsync pumps the host, no popup while it runs");
    s_st.state = UI_BACKUP_DONE;
    s_st.done = 10;
    snprintf(s_st.name, sizeof(s_st.name), "ico-pc-backup-2026-11-01.zip");
    ui_settings_vsync();
    CHECK(popupIs("Your data is backed up", "10 files saved to\nico-pc-backup-2026-11-01.zip"),
          "Back up: the done popup \"%s\" / \"%s\"", ui_popup_title(), ui_popup_body());
    ui_popup_reset();
    ui_settings_vsync();
    CHECK(!ui_popup_active(), "Back up: the done popup comes once");
    frame(0);
    CHECK(noteStarting(UI_PAGE_EXTRAS, "Saves your memory card") >= 0, "Back up: its note again");

    /* refused: the reason */
    press(lt_ext_confirm_button());
    CHECK(s_begins == 2, "Back up: a second backup");
    s_st.state = UI_BACKUP_FAILED;
    s_st.why = UI_BACKUP_WHY_BUSY;
    ui_settings_vsync();
    CHECK(popupIs("Could not back up your data",
                  "Not now: the game is saving. Try again in a moment."),
          "Back up: the failed popup \"%s\" / \"%s\"", ui_popup_title(), ui_popup_body());
    ui_popup_reset();

    /* the dialog closed: nothing */
    press(lt_ext_confirm_button());
    s_st.state = UI_BACKUP_CANCELLED;
    ui_settings_vsync();
    CHECK(s_begins == 3 && !ui_popup_active(), "Back up: a closed dialog shows nothing");

    /* the host busy: nothing pending */
    s_beginAnswer = -1;
    press(lt_ext_confirm_button());
    s_st.state = UI_BACKUP_FAILED;
    ui_settings_vsync();
    CHECK(s_begins == 4 && !ui_popup_active(), "Back up: a begin refused leaves nothing pending");
    s_beginAnswer = 0;
}

static void testRestore(void)
{
    const int hz = ui_popup_vsync_hz();
    int exL;

    resetHost();
    exL = onRow(UI_OPT_EXTRAS_RESTORE);
    press(lt_ext_confirm_button());
    CHECK(s_begins == 0, "Restore: the first press asks nothing of the host");
    CHECK(popupIs("Restore from a backup?", NULL) &&
              strncmp(ui_popup_body(),
                      "The memory card, save states, settings and achievements here", 60) == 0,
          "Restore: what it replaces \"%s\" / \"%s\"", ui_popup_title(), ui_popup_body());
    ui_popup_reset();
    frame(0);
    CHECK(strcmp(labelOf(UI_OPT_EXTRAS_RESTORE), "Press again to choose a backup") == 0,
          "Restore: the label asks for a second press (\"%s\")", labelOf(UI_OPT_EXTRAS_RESTORE));
    /* it runs out */
    for (int i = 0; i < 8 * hz + 2; i++) {
        ui_settings_vsync();
    }
    frame(0);
    CHECK(strcmp(labelOf(UI_OPT_EXTRAS_RESTORE), "Restore from a backup") == 0,
          "Restore: the first press runs out");
    press(lt_ext_confirm_button());
    CHECK(s_begins == 0, "Restore: after it ran out, a press is a first press again");
    ui_popup_reset();
    /* the cursor moves off */
    press(LT_PAD_UP);
    press(LT_PAD_DOWN);
    CHECK(lt_ext_layout(exL)->curItem ==
                  ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_RESTORE) &&
              strcmp(labelOf(UI_OPT_EXTRAS_RESTORE), "Restore from a backup") == 0,
          "Restore: moving off ends the first press");
    press(lt_ext_confirm_button());
    CHECK(s_begins == 0, "Restore: so the next press is a first one");
    ui_popup_reset();
    press(lt_ext_confirm_button());
    CHECK(s_begins == 1 && s_lastJob == UI_BACKUP_JOB_RESTORE,
          "Restore: the second press asks for the file dialog (%d, job %d)", s_begins, s_lastJob);
    frame(0);
    CHECK(noteStarting(UI_PAGE_EXTRAS, "Choose the backup to restore.") >= 0 &&
              strcmp(labelOf(UI_OPT_EXTRAS_RESTORE), "Restore from a backup") == 0,
          "Restore: the dialog's note");

    /* the backup read */
    s_st.state = UI_BACKUP_WORKING;
    frame(0);
    CHECK(noteStarting(UI_PAGE_EXTRAS, "Reading the backup") >= 0, "Restore: reading");
    s_st.state = UI_BACKUP_ASK;
    s_st.otherDisc = 1;
    snprintf(s_st.version, sizeof(s_st.version), "v0.9.7");
    snprintf(s_st.date, sizeof(s_st.date), "2026-10-31 20:15");
    snprintf(s_st.discs, sizeof(s_st.discs), "SCES-50760, SCPS-11003");
    snprintf(s_st.name, sizeof(s_st.name), "old.zip");
    ui_settings_vsync();
    CHECK(popupIs("Restore this backup?",
                  "Version: v0.9.7\nSaved: 2026-10-31 20:15\nDiscs: SCES-50760, SCPS-11003\nMade "
                  "while another disc was played\nPress again to restore it"),
          "Restore: the backup's popup \"%s\" / \"%s\"", ui_popup_title(), ui_popup_body());
    ui_popup_reset();
    ui_settings_vsync();
    CHECK(!ui_popup_active(), "Restore: the backup's popup comes once");
    frame(0);
    CHECK(strcmp(labelOf(UI_OPT_EXTRAS_RESTORE), "Restore this backup") == 0 &&
              noteStarting(UI_PAGE_EXTRAS, "Made by version v0.9.7.") >= 0,
          "Restore: the label and the note ask to restore it (\"%s\")",
          labelOf(UI_OPT_EXTRAS_RESTORE));
    press(lt_ext_confirm_button());
    CHECK(s_proceeds == 1 && s_cancels == 0 && s_begins == 1,
          "Restore: the press restores it (%d, %d)", s_proceeds, s_cancels);
    s_st.done = 2;
    s_st.total = 9;
    frame(0);
    CHECK(noteStarting(UI_PAGE_EXTRAS, "Restoring: 2 of 9 files") >= 0, "Restore: the progress");
    s_st.state = UI_BACKUP_DONE;
    s_st.restarts = 1;
    ui_settings_vsync();
    CHECK(popupIs("Restoring your data", "The game restarts now and puts the backup in place."),
          "Restore: the done popup \"%s\" / \"%s\"", ui_popup_title(), ui_popup_body());
    ui_popup_reset();
    press(lt_ext_confirm_button());
    lt_ext_layout(exL)->curItem = ui_settings_row_of(UI_PAGE_EXTRAS, UI_OPT_EXTRAS_BACKUP);
    frame(0);
    press(lt_ext_confirm_button());
    CHECK(s_begins == 1 && s_proceeds == 1 && !ui_popup_active(),
          "Restore: nothing more is asked while the run ends");

    /* the phone's words: the game closes */
    resetHost();
    exL = onRow(UI_OPT_EXTRAS_RESTORE);
    press(lt_ext_confirm_button());
    press(lt_ext_confirm_button());
    s_st.state = UI_BACKUP_ASK;
    ui_settings_vsync();
    ui_popup_reset();
    press(lt_ext_confirm_button());
    s_st.state = UI_BACKUP_DONE;
    s_st.restarts = 0;
    ui_settings_vsync();
    CHECK(popupIs("Restoring your data",
                  "The game closes now. Start it again: it puts the backup in place as it starts."),
          "Restore: the closing popup \"%s\"", ui_popup_body());

    /* dropped: the cursor moves off, a minute passes */
    resetHost();
    exL = onRow(UI_OPT_EXTRAS_RESTORE);
    press(lt_ext_confirm_button());
    press(lt_ext_confirm_button());
    s_st.state = UI_BACKUP_ASK;
    ui_settings_vsync();
    ui_popup_reset();
    press(LT_PAD_UP);
    CHECK(s_cancels == 1 && s_proceeds == 0, "Restore: moving off drops the backup read (%d)",
          s_cancels);
    ui_settings_vsync();
    CHECK(!ui_popup_active(), "Restore: a dropped backup shows nothing");
    press(LT_PAD_DOWN);
    press(lt_ext_confirm_button());
    press(lt_ext_confirm_button());
    s_st.state = UI_BACKUP_ASK;
    for (int i = 0; i < 60 * hz + 3; i++) {
        ui_settings_vsync();
    }
    CHECK(s_cancels == 2 && s_proceeds == 0, "Restore: a minute drops it (%d)", s_cancels);
    ui_popup_reset();

    /* refused: the reason */
    press(lt_ext_confirm_button());
    ui_popup_reset();
    press(lt_ext_confirm_button());
    s_st.state = UI_BACKUP_FAILED;
    s_st.why = UI_BACKUP_WHY_NOT_BACKUP;
    ui_settings_vsync();
    CHECK(popupIs("Could not restore the backup", "This file is not a backup made by this game."),
          "Restore: the failed popup \"%s\" / \"%s\"", ui_popup_title(), ui_popup_body());
    ui_popup_reset();
    ui_settings_set_backup_host(NULL);
}

static void testStrings(void)
{
    static const UiStrId kRows[] = {UI_STR_EXTRAS_BACKUP, UI_STR_EXTRAS_RESTORE,
                                    UI_STR_BACKUP_NOTE,   UI_STR_RESTORE_NOTE,
                                    UI_STR_BACKUP_DONE,   UI_STR_RESTORE_CONFIRM_BODY};
    for (int l = 1; l < UI_LANG_COUNT; l++) {
        for (unsigned i = 0; i < sizeof(kRows) / sizeof(kRows[0]); i++) {
            const char *t = ui_str_in((UiLang)l, kRows[i]);
            CHECK(t[0] != '\0' && strcmp(t, ui_str_in(UI_LANG_EN, kRows[i])) != 0,
                  "language %d, string %d translated", l, kRows[i]);
        }
    }
    /* the row names fit their box of 300 at 60 % or more */
    for (int g = 0; g < UI_LANG_COUNT; g++) {
        ui_set_language((UiLang)g);
        for (int k = 0; k < 3; k++) {
            const UiStrId id = k == 0   ? UI_STR_EXTRAS_BACKUP
                               : k == 1 ? UI_STR_EXTRAS_RESTORE
                                        : UI_STR_RESTORE_AGAIN;
            const float w = ui_measure_menu_text(UI_MENU_TEXT_SIZE, ui_str(id));
            CHECK(w * 0.6f <= 300.0f, "language %d: row %d fits (%.1f)", g, k, (double)w);
        }
    }
    ui_set_language(UI_LANG_EN);
}

/* another disc: the rows on its numbers, its confirm button */
static void testOtherDisc(const char *slug)
{
    CHECK(ico_region_select(slug) == 0, "the %s region", slug);
    resetHost();
    onRow(UI_OPT_EXTRAS_BACKUP);
    CHECK(shown(UI_OPT_EXTRAS_BACKUP) && shown(UI_OPT_EXTRAS_RESTORE), "%s: both rows shown", slug);
    press(lt_ext_confirm_button());
    CHECK(s_begins == 1 && s_lastJob == UI_BACKUP_JOB_SAVE, "%s: the disc's confirm backs up",
          slug);
    s_st.state = UI_BACKUP_CANCELLED;
    ui_settings_vsync();
    ui_settings_set_backup_host(NULL);
    CHECK(ico_region_select("pal") == 0, "the European region again");
}

int main(int argc, char **argv)
{
    snprintf(s_dir, sizeof(s_dir), "%s", argc > 1 ? argv[1] : ".");
    setEnv("LC_ALL", NULL);
    setEnv("LANG", "en_GB.UTF-8");
    if (argc > 2) {
        testOtherDisc(argv[2]);
        printf("settings_backup_test %s: %s\n", argv[2], failures ? "failed" : "ok");
        return failures ? 1 : 0;
    }
    testRows();
    testBackup();
    testRestore();
    testStrings();
    if (failures) {
        printf("settings_backup_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("settings_backup_test: ok\n");
    return 0;
}
