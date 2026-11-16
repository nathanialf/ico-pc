/* rd_glow_offset_test.c: how far the held glow's camera is from the
 * present's (rd_glow_offset.h; issue 123's right wall).  CPU only: a
 * screen-view of focal 512 about GS (2048, 2048) against itself (0), turned
 * by a yaw of tan 0.75 (384 GS pixels), against its eye moved alone (0: a
 * direction), a pitch of tan 0.25 (128); a view whose centre direction is
 * behind the other's eye refused; the median and largest over a window, the
 * refused counted, the line word for word. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rd_glow_offset.h"

static int failures;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static int near(double a, double b)
{
    return fabs(a - b) < 1e-3;
}

/* x_s = 2048 + 512 x / z, y_s = 2048 + 512 y / z, w = z, through the view
   world -> view given by rotation rot (row-major 3 x 3) and eye e:
   view = rot (world - e); column-major out */
static void screen_view(const double rot[9], const double e[3], float out[16])
{
    static const double p[4][4] = {
        {512, 0, 2048, 0}, {0, 512, 2048, 0}, {0, 0, 1, 0}, {0, 0, 1, 0}}; /* rows */
    double v[4][4]; /* rows: [rot | -rot e] */
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            v[r][c] = rot[r * 3 + c];
        }
        v[r][3] = -(rot[r * 3] * e[0] + rot[r * 3 + 1] * e[1] + rot[r * 3 + 2] * e[2]);
    }
    v[3][0] = v[3][1] = v[3][2] = 0.0;
    v[3][3] = 1.0;
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            double s = 0.0;
            for (int k = 0; k < 4; k++) {
                s += p[r][k] * v[k][c];
            }
            out[c * 4 + r] = (float)s;
        }
    }
}

int main(void)
{
    static const double id[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    /* a yaw: x' = 0.8 x - 0.6 z, z' = 0.6 x + 0.8 z: the old forward (0, 0,
       1) at x' / z' = -0.75 */
    static const double yaw[9] = {0.8, 0, -0.6, 0, 1, 0, 0.6, 0, 0.8};
    /* a pitch of tan 0.25: y' = c y - s z, z' = s y + c z, s / c = 0.25 */
    const double c = 1.0 / sqrt(1.0625), s = 0.25 / sqrt(1.0625);
    const double pitch[9] = {1, 0, 0, 0, c, -s, 0, s, c};
    static const double e0[3] = {0, 0, 0}, e1[3] = {300, -40, 120};
    float a[16], b[16];
    double px = -1.0;

    screen_view(id, e0, a);
    CHECK(rd_glow_offset(a, a, &px) && near(px, 0.0));
    screen_view(yaw, e0, b);
    CHECK(rd_glow_offset(a, b, &px) && near(px, 384.0));
    screen_view(id, e1, b); /* the eye moved alone: the direction stays */
    CHECK(rd_glow_offset(a, b, &px) && near(px, 0.0));
    screen_view(pitch, e1, b);
    CHECK(rd_glow_offset(a, b, &px) && near(px, 128.0));
    /* turned half round: the centre direction behind the eye */
    {
        static const double back[9] = {-1, 0, 0, 0, 1, 0, 0, 0, -1};
        screen_view(back, e0, b);
        CHECK(!rd_glow_offset(a, b, &px));
        float z[16];
        memset(z, 0, sizeof z);
        CHECK(!rd_glow_offset(z, a, &px)); /* no direction at the centre */
    }

    RdGlowOffsets o;
    char t[256];
    rd_glow_offsets_reset(&o);
    CHECK(rd_glow_offsets_text(&o, t, sizeof t) == 0 && t[0] == '\0');
    rd_glow_offsets_add(&o, 4.5);
    rd_glow_offsets_add(&o, 0.5);
    rd_glow_offsets_add(&o, 1.2);
    rd_glow_offsets_add(&o, 2.0);
    o.refused = 2;
    (void)rd_glow_offsets_text(&o, t, sizeof t);
    CHECK(strcmp(t, "; the held glow's camera against the present's (the frame centre's "
                    "direction): median 1.6, largest 4.5 GS pixels over 4 held presents, 2 not "
                    "taken") == 0);
    printf("%s\n", t);
    rd_glow_offsets_reset(&o);
    o.refused = 3;
    (void)rd_glow_offsets_text(&o, t, sizeof t);
    CHECK(strcmp(t, "; the held glow's camera against the present's: 3 held presents not taken") ==
          0);
    /* past the window's 512: every one counted and the largest kept, the
       median of the first 512 */
    rd_glow_offsets_reset(&o);
    for (unsigned i = 0; i < 600u; i++) {
        rd_glow_offsets_add(&o, i < 512u ? 1.0 : 9.0);
    }
    (void)rd_glow_offsets_text(&o, t, sizeof t);
    CHECK(strstr(t, "median 1.0, largest 9.0 GS pixels over 600 held presents") != NULL);
    if (failures) {
        printf("rd_glow_offset_test: %d failure(s)\n", failures);
        return 1;
    }
    printf("rd_glow_offset_test: ok\n");
    return 0;
}
