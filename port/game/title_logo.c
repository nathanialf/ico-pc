/*
 * port/game/title_logo.c
 *
 * The title's logo under the port's menus: title_logo.h.
 */
#include <string.h>

#include "title_logo.h"
#include "model_viewer.h"
#include "settings.h"

extern int stage_no;          /* common/src/main.c */
extern int current_layout_id; /* common/src/layout_texture.c */

#define TITLE_STAGE 1
/* the title's menu: Continue and New Game, New Game only */
#define LAYOUT_TITLE_CONTINUE 12
#define LAYOUT_TITLE_NEW 13

int ico_title_logo_model(const char *model)
{
    static const char *const kModels[] = {"I",   "C",    "O",    "I_f", "C_f",
                                          "O_f", "I_sd", "C_sd", "O_sd"};
    for (unsigned i = 0; i < sizeof(kModels) / sizeof(kModels[0]); i++) {
        if (strcmp(model, kModels[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

int ico_title_logo_step(int *hidden, int stage, int layout, int covered)
{
    if (stage != TITLE_STAGE) {
        *hidden = 0;
    } else if (covered) {
        *hidden = 1;
    } else if (layout == LAYOUT_TITLE_CONTINUE || layout == LAYOUT_TITLE_NEW) {
        *hidden = 0;
    }
    return *hidden;
}

static int s_hidden;

void ico_title_logo_update(void)
{
    /* every Main tick, so a stage without the logo clears the state */
    int covered = 0;
    if (stage_no == TITLE_STAGE) {
        const int list = ico_mv_title_list_layout();
        covered = ui_settings_covers_title() || (list >= 0 && current_layout_id == list);
    }
    ico_title_logo_step(&s_hidden, stage_no, current_layout_id, covered);
}

int ico_title_logo_skip(const char *model)
{
    return s_hidden && ico_title_logo_model(model);
}

int ico_title_stretch_model(const char *model)
{
    /* the title stage's models built to cover the 4:3 screen */
    static const char *const kModels[] = {"title_back"};
    if (stage_no != TITLE_STAGE) {
        return 0;
    }
    for (unsigned i = 0; i < sizeof(kModels) / sizeof(kModels[0]); i++) {
        if (strcmp(model, kModels[i]) == 0) {
            return 1;
        }
    }
    return 0;
}
