/* subtitles_test.c: text subtitle packs keyed by the game's subtitle blocks. */
#include <stdio.h>
#include <string.h>
#include "options.h"
#include "subtitles.h"

static int failures;

#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    static const int blocks[] = {0,  1,  2,  4,  5,  6,  7,  8,  9,  11, 12, 13, 19, 25,
                                 26, 27, 29, 31, 34, 35, 36, 37, 38, 43, 48, 49, 86, 88,
                                 91, 93, 96, 97, 98, 99, 100, 104, 105, 106, 109, 110, 111,
                                 112};
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        CHECK(ui_subtitle_text("rus", blocks[i]) != NULL);
    }
    CHECK(ui_subtitle_pack_count() >= 1);
    const int rus = ui_subtitle_pack_index("rus");
    CHECK(rus >= 0);
    if (rus >= 0) {
        const char *name = ui_subtitle_pack_name((size_t)rus);
        CHECK(name != NULL && name[0] != '\0');
    }
    CHECK(ui_subtitle_text("game", 0) == NULL);
    CHECK(ui_subtitle_text("rus", 3) == NULL);
    CHECK(ui_subtitle_text("rus", -1) == NULL);
    CHECK(ui_subtitle_text("missing", 0) == NULL);
    CHECK(strcmp(ui_subtitle_effective_language(ICO_SUBTITLE_SOURCE_DISC, "rus"), "game") == 0);
    CHECK(strcmp(ui_subtitle_effective_language(ICO_SUBTITLE_SOURCE_TEXT, "game"), "rus") == 0);
    CHECK(strcmp(ui_subtitle_effective_language(ICO_SUBTITLE_SOURCE_ALL, "missing"), "game") ==
          0);
    static const int runicBlocks[] = {9, 29, 31, 48, 49, 86, 91, 112};
    for (size_t i = 0; i < sizeof(runicBlocks) / sizeof(runicBlocks[0]); i++) {
        CHECK(ui_subtitle_block_uses_disc(runicBlocks[i], 0));
        CHECK(!ui_subtitle_block_uses_disc(runicBlocks[i], 1));
    }
    CHECK(!ui_subtitle_block_uses_disc(88, 0));
    const char *line = ui_subtitle_text("rus", 1);
    CHECK(line != NULL && strchr(line, '\n') != NULL);

    if (failures != 0) {
        fprintf(stderr, "subtitles_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("subtitles_test: ok (%zu built-in Russian lines)\n",
           sizeof(blocks) / sizeof(blocks[0]));
    return 0;
}
