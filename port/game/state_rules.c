/*
 * port/game/state_rules.c
 *
 * The save states' decisions without the game (state_rules.h).
 */
#include "state_rules.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"

int ico_state_on_pause_menu(const IcoStateConds *c)
{
    return c->paused != 0 && c->stage > c->stage_title &&
           (c->layout == c->layout_pause || c->layout >= c->layout_count);
}

IcoStateResult ico_state_conditions(const IcoStateConds *c, int saving, const char **what)
{
    const char *w = "";
    IcoStateResult r = ICO_STATE_OK;

    if (!c->available) {
        r = ICO_STATE_OFF;
    } else if (saving && !ico_state_on_pause_menu(c)) {
        r = ICO_STATE_NOT_PAUSED;
    } else if (c->stage_switch != 0) {
        r = ICO_STATE_STAGE_SWITCH;
    } else if (c->card_busy) {
        r = ICO_STATE_CARD_BUSY;
    } else if (!c->disc_idle) {
        r = ICO_STATE_DISC_BUSY;
    } else if (c->movie) {
        r = ICO_STATE_MOVIE;
    } else if (c->photo || c->credits || c->gallery) {
        r = ICO_STATE_PHOTO;
        w = c->photo ? "photo mode" : c->credits ? "the credits" : "the music gallery";
    }
    if (what != NULL) {
        *what = w;
    }
    return r;
}

const char *ico_state_result_text(IcoStateResult r)
{
    switch (r) {
    case ICO_STATE_OK:
        return "ok";
    case ICO_STATE_OFF:
        return "save states are off on this build";
    case ICO_STATE_NOT_PAUSED:
        return "not on the pause menu";
    case ICO_STATE_STAGE_SWITCH:
        return "a stage switch is under way";
    case ICO_STATE_CARD_BUSY:
        return "the memory card is busy";
    case ICO_STATE_DISC_BUSY:
        return "the disc is busy";
    case ICO_STATE_MOVIE:
        return "a movie is playing";
    case ICO_STATE_PHOTO:
        return "another mode is on";
    case ICO_STATE_EMPTY:
        return "the slot is empty";
    case ICO_STATE_BAD_FILE:
        return "the file is damaged or not a state";
    case ICO_STATE_OTHER_BUILD:
        return "saved by another build";
    case ICO_STATE_OTHER_DISC:
        return "saved from another disc";
    case ICO_STATE_OTHER_PLATFORM:
        return "saved on another platform";
    case ICO_STATE_OTHER_SETTINGS:
        return "settings differ";
    case ICO_STATE_SELF_CHECK:
        return "the restored memory failed its checks";
    case ICO_STATE_IO:
        return "the file could not be written";
    case ICO_STATE_BUSY:
        return "a save or load is in flight";
    }
    return "unknown";
}

int ico_state_slot_path(char *out, size_t size, const char *dir, int slot, const char *ext)
{
    int n;

    if (size > 0) {
        out[0] = '\0';
    }
    if (slot < 1 || slot > ICO_STATE_SLOTS || dir == NULL || dir[0] == '\0') {
        return -1;
    }
    n = snprintf(out, size, "%s/slot%d%s", dir, slot, ext);
    if (n < 0 || (size_t)n >= size) {
        if (size > 0) {
            out[0] = '\0';
        }
        return -1;
    }
    return 0;
}

static int file_exists(const char *path)
{
    FILE *f = ico_fopen(path, "rb");

    if (f == NULL) {
        return 0;
    }
    fclose(f);
    return 1;
}

void ico_state_list_dir(const char *dir, const IcoStateHeader *now,
                        IcoStateSlotInfo out[ICO_STATE_SLOTS])
{
    /* the header is about 6 KB with its section table: one at a time */
    IcoStateHeader *h = (IcoStateHeader *)malloc(sizeof *h);
    int i;

    for (i = 0; i < ICO_STATE_SLOTS; i++) {
        IcoStateSlotInfo *s = &out[i];
        char path[1024];
        IcoStateResult r;

        memset(s, 0, sizeof *s);
        s->why_not = ICO_STATE_EMPTY;
        if (h == NULL || ico_state_slot_path(path, sizeof path, dir, i + 1, ".ics") != 0) {
            continue;
        }
        r = ico_state_file_read_header(path, h);
        if (r == ICO_STATE_EMPTY) {
            continue;
        }
        s->present = 1;
        if (r != ICO_STATE_OK) {
            s->why_not = r;
            continue;
        }
        s->play_frames = h->play_frames;
        s->stage_no = h->stage_no;
        s->saved_at = (long long)h->saved_at;
        snprintf(s->build_commit, sizeof s->build_commit, "%s", h->commit);
        r = now != NULL ? ico_state_header_check(h, now, NULL, 0) : ICO_STATE_OFF;
        s->loadable = r == ICO_STATE_OK;
        s->why_not = r;
        if (ico_state_slot_path(path, sizeof path, dir, i + 1, ".png") == 0 &&
            strlen(path) < sizeof s->png_path && file_exists(path)) {
            memcpy(s->png_path, path, strlen(path) + 1);
        }
    }
    free(h);
}

int ico_state_parse_at(const char *s, unsigned int *tick, int *slot)
{
    char *end;
    unsigned long long t;
    long n = 0;

    *tick = 0;
    if (slot != NULL) {
        *slot = 0;
    }
    if (s == NULL || s[0] < '0' || s[0] > '9') {
        return -1;
    }
    t = strtoull(s, &end, 10);
    if (t == 0 || t > 0xFFFFFFFFull) {
        return -1;
    }
    if (slot != NULL) {
        if (*end != ':' || end[1] < '0' || end[1] > '9') {
            return -1;
        }
        n = strtol(end + 1, &end, 10);
        if (n < 1 || n > ICO_STATE_SLOTS) {
            return -1;
        }
    }
    if (*end != '\0') {
        return -1;
    }
    *tick = (unsigned int)t;
    if (slot != NULL) {
        *slot = (int)n;
    }
    return 0;
}

void ico_state_thumb_size(int w, int h, int *dw, int *dh)
{
    int ow = w < ICO_STATE_THUMB_W ? w : ICO_STATE_THUMB_W;
    long long oh;

    if (ow < 1) {
        ow = 1;
    }
    oh = w > 0 ? ((long long)h * ow + w / 2) / w : 1;
    *dw = ow;
    *dh = oh < 1 ? 1 : (int)oh;
}

void ico_state_thumb_scale(const uint8_t *src, int w, int h, uint8_t *dst, int dw, int dh)
{
    int x, y;

    for (y = 0; y < dh; y++) {
        const int y0 = (int)((long long)y * h / dh);
        int y1 = (int)((long long)(y + 1) * h / dh);

        y1 = y1 > y0 ? y1 : y0 + 1;
        for (x = 0; x < dw; x++) {
            const int x0 = (int)((long long)x * w / dw);
            int x1 = (int)((long long)(x + 1) * w / dw);
            unsigned long sum[3] = {0, 0, 0};
            unsigned long n;
            int sx, sy, c;

            x1 = x1 > x0 ? x1 : x0 + 1;
            n = (unsigned long)(x1 - x0) * (unsigned long)(y1 - y0);
            for (sy = y0; sy < y1; sy++) {
                const uint8_t *p = src + ((size_t)sy * (size_t)w + (size_t)x0) * 4;

                for (sx = x0; sx < x1; sx++, p += 4) {
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                }
            }
            for (c = 0; c < 3; c++) {
                dst[((size_t)y * (size_t)dw + (size_t)x) * 4 + (size_t)c] =
                    (uint8_t)((sum[c] + n / 2) / n);
            }
            dst[((size_t)y * (size_t)dw + (size_t)x) * 4 + 3] = 255;
        }
    }
}
