/*
 * port/platform/test/desktop_entry_test.c
 *
 * The applications-menu entry (desktop_entry.c): the exact file text, the
 * Exec quoting for a path with a space, '$', '"', '\', '`' and '%', the
 * refusals (empty, a newline in the path, too small a buffer), the data
 * folder rule (XDG_DATA_HOME absolute, else HOME/.local/share), and an
 * install over a fake data folder: both files in place under the app id's
 * name, the PNG copied byte for byte, no .tmp left, a second install
 * replacing the first, and nothing written when the picture is missing.
 * argv[1]: a folder the test may write in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "desktop_entry.h"
#include "host_fs.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* the entry for exe equals the text with the given Exec line */
static void exec_case(const char *exe, const char *execLine)
{
    char got[1024], want[1024];

    snprintf(want, sizeof(want),
             "[Desktop Entry]\nType=Application\nName=ICO\n%s\nIcon=com.defnf.icopc\n"
             "Categories=Game;\nTerminal=false\nStartupWMClass=com.defnf.icopc\n",
             execLine);
    CHECK(ico_desktop_entry_text(got, sizeof(got), exe) == 0);
    if (strcmp(got, want) != 0) {
        printf("FAIL exec for [%s]:\n--- got\n%s--- want\n%s", exe, got, want);
        fails++;
    }
}

static int slurp(const char *path, char *buf, size_t cap, size_t *n)
{
    FILE *f = ico_fopen(path, "rb");

    if (f == NULL) {
        return -1;
    }
    *n = fread(buf, 1, cap, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    char base[1024], p[2400], got[2048];
    const char png[] = "\x89PNG\r\n\x1a\n-not really a picture-\0\xff";
    size_t n;
    FILE *f;

    if (argc < 2) {
        printf("usage: desktop_entry_test <scratch folder>\n");
        return 2;
    }

    exec_case("/games/ico_pc", "Exec=\"/games/ico_pc\"");
    exec_case("/games/my dir/ico_pc", "Exec=\"/games/my dir/ico_pc\"");
    /* the Exec quoting's backslash, then every backslash doubled by the
       string value's escaping (the specification unescapes the string
       first): a dollar, a double quote and a backtick take two
       backslashes, a literal backslash is four */
    exec_case("/g/a$b", "Exec=\"/g/a\\\\$b\"");
    exec_case("/g/a\"b", "Exec=\"/g/a\\\\\"b\"");
    exec_case("/g/a\\b", "Exec=\"/g/a\\\\\\\\b\"");
    exec_case("/g/a`b", "Exec=\"/g/a\\\\`b\"");
    exec_case("/g/100%/x", "Exec=\"/g/100%%/x\"");
    exec_case("/g/$\"\\%", "Exec=\"/g/\\\\$\\\\\"\\\\\\\\%%\"");
    exec_case("C:\\\\x", "Exec=\"C:\\\\\\\\\\\\\\\\x\""); /* two literal backslashes: eight */

    /* refusals: empty, NULL, a line break, no room (out left empty) */
    strcpy(got, "x");
    CHECK(ico_desktop_entry_text(got, sizeof(got), "") == -1 && got[0] == '\0');
    CHECK(ico_desktop_entry_text(got, sizeof(got), NULL) == -1);
    CHECK(ico_desktop_entry_text(got, sizeof(got), "/a\nExec=/bin/sh") == -1 && got[0] == '\0');
    CHECK(ico_desktop_entry_text(got, sizeof(got), "/a\rb") == -1);
    CHECK(ico_desktop_entry_text(got, 20, "/games/ico_pc") == -1 && got[0] == '\0');
    CHECK(ico_desktop_entry_text(got, 0, "/games/ico_pc") == -1);

    /* the data folder rule */
    CHECK(setenv("XDG_DATA_HOME", "/xdg/data", 1) == 0 && setenv("HOME", "/home/p", 1) == 0);
    CHECK(ico_desktop_entry_data_home(got, sizeof(got)) == 0 && strcmp(got, "/xdg/data") == 0);
    CHECK(setenv("XDG_DATA_HOME", "relative/path", 1) == 0);
    CHECK(ico_desktop_entry_data_home(got, sizeof(got)) == 0 &&
          strcmp(got, "/home/p/.local/share") == 0);
    CHECK(unsetenv("XDG_DATA_HOME") == 0);
    CHECK(ico_desktop_entry_data_home(got, sizeof(got)) == 0 &&
          strcmp(got, "/home/p/.local/share") == 0);
    CHECK(ico_desktop_entry_data_home(got, 8) == -1 && got[0] == '\0');
    CHECK(unsetenv("HOME") == 0);
    CHECK(ico_desktop_entry_data_home(got, sizeof(got)) == -1 && got[0] == '\0');

    /* an install over a fake data folder */
    snprintf(base, sizeof(base), "%s/desktop_entry_scratch", argv[1]);
    ico_mkdir(argv[1]);
    ico_mkdir(base);
    snprintf(p, sizeof(p), "%s/in.png", base);
    f = ico_fopen(p, "wb");
    CHECK(f != NULL);
    if (f == NULL) {
        return 1;
    }
    fwrite(png, 1, sizeof(png), f);
    fclose(f);

    {
        char data[1100], entry[1200], pic[1200], pngIn[1200];

        snprintf(data, sizeof(data), "%s/share", base);
        snprintf(pngIn, sizeof(pngIn), "%s/in.png", base);
        snprintf(entry, sizeof(entry), "%s/applications/com.defnf.icopc.desktop", data);
        snprintf(pic, sizeof(pic), "%s/icons/hicolor/256x256/apps/com.defnf.icopc.png", data);

        /* a missing picture: refused, nothing written */
        snprintf(p, sizeof(p), "%s/none.png", base);
        CHECK(ico_desktop_entry_install(data, "/games/my dir/ico_pc", p) == -1);
        CHECK(ico_path_kind(entry, NULL, NULL) == -1);

        CHECK(ico_desktop_entry_install(data, "/games/my dir/ico_pc", pngIn) == 0);
        CHECK(slurp(entry, got, sizeof(got) - 1, &n) == 0);
        got[n] = '\0';
        CHECK(strstr(got, "Exec=\"/games/my dir/ico_pc\"\n") != NULL);
        CHECK(strncmp(got, "[Desktop Entry]\nType=Application\nName=ICO\n", 41) == 0);
        CHECK(slurp(pic, got, sizeof(got), &n) == 0 && n == sizeof(png) &&
              memcmp(got, png, n) == 0);
        snprintf(p, sizeof(p), "%s.tmp", entry);
        CHECK(ico_path_kind(p, NULL, NULL) == -1);
        snprintf(p, sizeof(p), "%s.tmp", pic);
        CHECK(ico_path_kind(p, NULL, NULL) == -1);

        /* a second install replaces the first */
        CHECK(ico_desktop_entry_install(data, "/other/ico_pc", pngIn) == 0);
        CHECK(slurp(entry, got, sizeof(got) - 1, &n) == 0);
        got[n] = '\0';
        CHECK(strstr(got, "Exec=\"/other/ico_pc\"\n") != NULL && strstr(got, "my dir") == NULL);

        /* no picture named: the entry alone */
        ico_remove(pic);
        CHECK(ico_desktop_entry_install(data, "/third/ico_pc", NULL) == 0);
        CHECK(ico_path_kind(pic, NULL, NULL) == -1);
        CHECK(slurp(entry, got, sizeof(got) - 1, &n) == 0);
        got[n] = '\0';
        CHECK(strstr(got, "Exec=\"/third/ico_pc\"\n") != NULL);

        /* refusals */
        CHECK(ico_desktop_entry_install(NULL, "/x", pngIn) == -1);
        CHECK(ico_desktop_entry_install("", "/x", pngIn) == -1);
        CHECK(ico_desktop_entry_install(data, "", pngIn) == -1);

        ico_remove(entry);
        ico_remove(pic);
    }

    printf(fails ? "desktop_entry_test: %d failures\n" : "desktop_entry_test ok\n", fails);
    return fails != 0;
}
