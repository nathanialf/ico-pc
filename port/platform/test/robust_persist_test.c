/*
 * port/platform/test/robust_persist_test.c
 *
 * The port's own [dev] gpu_robust turning itself off (robust_persist.h):
 *   - the mark's text written and read back, and a hand-written or
 *     foreign value never taken for the game's;
 *   - the rule over sequences of starts: three clean starts in a row turn
 *     it off, a loss or an end while preparing resets the count, a start
 *     that never reached the game leaves it, another version turns it off
 *     at once, no mark does nothing;
 *   - the mark through the config text (ico_toml set, render, parse) and
 *     the keys' removal leaving every other line byte for byte, a
 *     player's gpu_robust (no mark) included in what the removal is not
 *     asked to touch;
 *   - the log lines word for word.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host_config.h"
#include "robust_persist.h"

static int g_fail;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);               \
            g_fail = 1;                                                                            \
        }                                                                                          \
    } while (0)

static void testMark(void)
{
    RobustMark m, n;
    char text[96];

    memset(&m, 0, sizeof(m));
    snprintf(m.version, sizeof(m.version), "0.10.2");
    m.clean = 1;
    CHECK(robust_mark_format(text, sizeof(text), &m) == 0);
    CHECK(strcmp(text, "losses; 0.10.2; 1; -") == 0);
    CHECK(robust_mark_parse(text, &n) == 0 && strcmp(n.version, "0.10.2") == 0 && n.clean == 1 &&
          !n.running);
    m.running = 1;
    CHECK(robust_mark_format(text, sizeof(text), &m) == 0 &&
          strcmp(text, "losses; 0.10.2; 1; run") == 0);
    CHECK(robust_mark_parse(text, &n) == 0 && n.running);
    CHECK(robust_mark_parse("losses;0.10.2;2;run", &n) == 0 && n.clean == 2);
    CHECK(robust_mark_format(text, 8, &m) == -1);
    /* not the game's */
    CHECK(robust_mark_parse(NULL, &n) == -1);
    CHECK(robust_mark_parse("", &n) == -1);
    CHECK(robust_mark_parse("by hand", &n) == -1);
    CHECK(robust_mark_parse("crash; 0.10.2; 1; -", &n) == -1);
    CHECK(robust_mark_parse("losses; ; 1; -", &n) == -1);
    CHECK(robust_mark_parse("losses; 0.10.2; x; -", &n) == -1);
    CHECK(robust_mark_parse("losses; 0.10.2; 9; -", &n) == -1);
    CHECK(robust_mark_parse("losses; 0.10.2; -1; -", &n) == -1);
    CHECK(robust_mark_parse("losses; 0.10.2", &n) == -1);
}

/* one start of the rule; the mark carried to the next start with "run"
   set when the start reached the game */
static RobustStep start(RobustMark *mark, const char *version, int lost, int ended, int reached)
{
    const RobustStep s = robust_persist_step(mark, version, lost, ended);
    if (s.action == ROBUST_STAYS) {
        *mark = s.mark;
        mark->running = reached;
    }
    return s;
}

static void testRule(void)
{
    RobustMark m;
    RobustStep s;

    /* no mark: nothing */
    CHECK(robust_persist_step(NULL, "0.10.2", 0, 0).action == ROBUST_NONE);
    CHECK(robust_persist_step(NULL, "0.10.2", 1, 1).action == ROBUST_NONE);

    /* written by the start that lost twice: 0 clean, and it reaches the game */
    memset(&m, 0, sizeof(m));
    snprintf(m.version, sizeof(m.version), "0.10.2");
    m.running = 1;
    s = start(&m, "0.10.2", 0, 0, 1);
    CHECK(s.action == ROBUST_STAYS && s.mark.clean == 1);
    s = start(&m, "0.10.2", 0, 0, 1);
    CHECK(s.action == ROBUST_STAYS && s.mark.clean == 2);
    s = start(&m, "0.10.2", 0, 0, 1);
    CHECK(s.action == ROBUST_OFF_CLEAN);

    /* a loss resets the count */
    memset(&m, 0, sizeof(m));
    snprintf(m.version, sizeof(m.version), "0.10.2");
    m.running = 1;
    s = start(&m, "0.10.2", 0, 0, 1);
    s = start(&m, "0.10.2", 0, 0, 1);
    CHECK(s.mark.clean == 2);
    s = start(&m, "0.10.2", 1, 0, 1);
    CHECK(s.action == ROBUST_STAYS && s.mark.clean == 0);
    s = start(&m, "0.10.2", 0, 0, 1);
    CHECK(s.mark.clean == 1);
    /* an end while preparing the graphics resets it too */
    s = start(&m, "0.10.2", 0, 1, 1);
    CHECK(s.action == ROBUST_STAYS && s.mark.clean == 0);

    /* a start that never reached the game and ended without a word: the
       count stays (it is not clean, and not a loss) */
    s = start(&m, "0.10.2", 0, 0, 1);
    CHECK(s.mark.clean == 1);
    s = start(&m, "0.10.2", 0, 0, 0); /* judges the previous; this one does not reach */
    CHECK(s.mark.clean == 2);
    s = start(&m, "0.10.2", 0, 0, 1);
    CHECK(s.action == ROBUST_STAYS && s.mark.clean == 2);
    s = start(&m, "0.10.2", 0, 0, 1);
    CHECK(s.action == ROBUST_OFF_CLEAN);

    /* three clean starts needed even after two resets */
    memset(&m, 0, sizeof(m));
    snprintf(m.version, sizeof(m.version), "0.10.2");
    m.running = 1;
    CHECK(start(&m, "0.10.2", 1, 0, 1).mark.clean == 0);
    CHECK(start(&m, "0.10.2", 1, 0, 1).mark.clean == 0);
    CHECK(start(&m, "0.10.2", 0, 0, 1).mark.clean == 1);
    CHECK(start(&m, "0.10.2", 0, 0, 1).mark.clean == 2);
    CHECK(start(&m, "0.10.2", 0, 0, 1).action == ROBUST_OFF_CLEAN);

    /* another version: off at once, whatever the count and the last end */
    memset(&m, 0, sizeof(m));
    snprintf(m.version, sizeof(m.version), "0.10.1");
    CHECK(robust_persist_step(&m, "0.10.2", 0, 0).action == ROBUST_OFF_VERSION);
    CHECK(robust_persist_step(&m, "0.10.2", 1, 1).action == ROBUST_OFF_VERSION);
    CHECK(robust_persist_step(&m, "dev", 0, 0).action == ROBUST_OFF_VERSION);
    CHECK(robust_persist_step(&m, NULL, 0, 0).action == ROBUST_OFF_VERSION);
}

static const char k_file[] = "version = 1\n"
                             "[video]\n"
                             "vsync = true # keep\n"
                             "[dev]\n"
                             "# a note\n"
                             "gpu_robust = true\n"
                             "slow_step_ms = 8.0\n"
                             "  gpu_robust_by = \"losses; 0.10.1; 2; run\"\n"
                             "[audio]\n"
                             "gpu_robust = 3\n"
                             "volume = 1.0\n";

static void testFile(void)
{
    IcoToml *t = ico_toml_parse(k_file);
    RobustMark m, n;
    char *text, *out, *again;
    char value[96];

    CHECK(t != NULL);
    CHECK(robust_mark_parse(ico_toml_get(t, "dev.gpu_robust_by"), &m) == 0 && m.clean == 2 &&
          m.running);
    /* the mark written back through the config text */
    m.running = 0;
    m.clean = 0;
    CHECK(robust_mark_format(value, sizeof(value), &m) == 0);
    CHECK(ico_toml_set_string(t, "dev.gpu_robust_by", value) == 0);
    text = ico_toml_render(t, k_file);
    CHECK(text != NULL && strstr(text, "gpu_robust_by = \"losses; 0.10.1; 0; -\"") != NULL);
    ico_toml_free(t);
    t = ico_toml_parse(text);
    CHECK(robust_mark_parse(ico_toml_get(t, "dev.gpu_robust_by"), &n) == 0 && n.clean == 0 &&
          !n.running && strcmp(n.version, "0.10.1") == 0);
    ico_toml_free(t);
    free(text);

    /* a new mark on a file that has the key and no mark */
    t = ico_toml_parse("[dev]\ngpu_robust = true\n");
    CHECK(robust_mark_parse(ico_toml_get(t, "dev.gpu_robust_by"), &n) == -1);
    CHECK(ico_toml_set_string(t, "dev.gpu_robust_by", "losses; 0.10.2; 0; -") == 0);
    text = ico_toml_render(t, "[dev]\ngpu_robust = true\n");
    CHECK(text != NULL &&
          strcmp(text, "[dev]\ngpu_robust = true\ngpu_robust_by = \"losses; 0.10.2; 0; -\"\n") ==
              0);
    free(text);
    ico_toml_free(t);

    /* the removal: both [dev] keys, nothing else (a gpu_robust in another
       section stays) */
    out = robust_persist_strip(k_file);
    CHECK(out != NULL);
    CHECK(strcmp(out, "version = 1\n[video]\nvsync = true # keep\n[dev]\n# a note\n"
                      "slow_step_ms = 8.0\n[audio]\ngpu_robust = 3\nvolume = 1.0\n") == 0);
    again = robust_persist_strip(out);
    CHECK(again != NULL && strcmp(again, out) == 0);
    free(again);
    free(out);
    /* no trailing newline, no dev section, empty text */
    out = robust_persist_strip("[dev]\nx = 1\ngpu_robust = true");
    CHECK(out != NULL && strcmp(out, "[dev]\nx = 1\n") == 0);
    free(out);
    out = robust_persist_strip("gpu_robust = true\n[video]\nvsync = true\n");
    CHECK(out != NULL && strcmp(out, "gpu_robust = true\n[video]\nvsync = true\n") == 0);
    free(out);
    out = robust_persist_strip("");
    CHECK(out != NULL && out[0] == '\0');
    free(out);
    out = robust_persist_strip("[dev]\r\ngpu_robust = true\r\nx = 1\r\n");
    CHECK(out != NULL && strcmp(out, "[dev]\r\nx = 1\r\n") == 0);
    free(out);
}

static void testLines(void)
{
    RobustMark old;
    RobustStep s;
    char line[512];

    memset(&old, 0, sizeof(old));
    snprintf(old.version, sizeof(old.version), "0.10.1");
    old.running = 1;
    old.clean = 1;
    s = robust_persist_step(&old, "0.10.1", 0, 0);
    CHECK(s.action == ROBUST_STAYS);
    CHECK(robust_persist_line(line, sizeof(line), &s, &old, "0.10.1", "config.toml") == 0);
    CHECK(strcmp(line, "ico_pc: robust access on (set by the game after 2 lost starts on 0.10.1): "
                       "2 clean starts of 3 before it turns off") == 0);
    old.clean = 2;
    s = robust_persist_step(&old, "0.10.1", 0, 0);
    CHECK(s.action == ROBUST_OFF_CLEAN);
    CHECK(robust_persist_line(line, sizeof(line), &s, &old, "0.10.1", "/x/config.toml") == 0);
    CHECK(strcmp(line, "ico_pc: robust access turned off (set by the game after 2 lost starts on "
                       "0.10.1): 3 clean starts in a row; [dev] gpu_robust and gpu_robust_by are "
                       "removed from /x/config.toml, and two lost starts in a row turn it on "
                       "again") == 0);
    s = robust_persist_step(&old, "0.10.2", 0, 0);
    CHECK(s.action == ROBUST_OFF_VERSION);
    CHECK(robust_persist_line(line, sizeof(line), &s, &old, "0.10.2", NULL) == 0);
    CHECK(strcmp(line, "ico_pc: robust access turned off (set by the game after 2 lost starts on "
                       "0.10.1): this is version 0.10.2, so those losses were another build's; "
                       "[dev] gpu_robust and gpu_robust_by are removed from config.toml, and two "
                       "lost starts in a row turn it on again") == 0);
    s = robust_persist_step(NULL, "0.10.2", 0, 0);
    CHECK(robust_persist_line(line, sizeof(line), &s, &old, "0.10.2", NULL) == -1);
    s = robust_persist_step(&old, "0.10.1", 0, 0);
    CHECK(robust_persist_line(line, 20, &s, &old, "0.10.1", NULL) == -1);
}

int main(void)
{
    testMark();
    testRule();
    testFile();
    testLines();
    if (g_fail) {
        return 1;
    }
    printf("robust_persist_test: ok\n");
    return 0;
}
