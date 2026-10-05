/* rd_replay_tool: loads an rd frame dump (rd_DumpFrame) and renders it
 * headless to a PNG, for the renderer verification plan (backend against
 * backend, before against after).
 *
 *   rd_replay_tool <dump> <out.png> [--target NAME] [--present WxH]
 *
 * NAME is a named target (SCENE, DISPLAY (default), SHADOW0..2, WORK0..3,
 * AA0, AA1, FEED128).  --present renders the Original presenter into a
 * W x H output and writes that instead.  Commands of later waves (meshes,
 * fog, ...) are skipped with a message instead of stopping.
 *
 * Exit: 0 written, 1 error, 77 no Vulkan device or no dump file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rd_internal.h"

static const char *const kNames[] = {"SCENE",   "DISPLAY", "SHADOW0", "SHADOW1",
                                     "SHADOW2", "WORK0",   "WORK1",   "WORK2",
                                     "WORK3",   "AA0",     "AA1",     "FEED128"};

static bool peekSize(const char *path, uint32_t *w, uint32_t *h)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return false;
    }
    uint8_t b[32];
    bool ok = fread(b, 1, sizeof(b), fp) == sizeof(b) && memcmp(b, RD_DUMP_MAGIC, 8) == 0;
    fclose(fp);
    if (ok) {
        *w = (uint32_t)b[24] | ((uint32_t)b[25] << 8) | ((uint32_t)b[26] << 16) |
             ((uint32_t)b[27] << 24);
        *h = (uint32_t)b[28] | ((uint32_t)b[29] << 8) | ((uint32_t)b[30] << 16) |
             ((uint32_t)b[31] << 24);
    }
    return ok;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <dump> <out.png> [--target NAME] [--present WxH]\n", argv[0]);
        return 1;
    }
    const char *dump = argv[1], *png = argv[2];
    int target = RD_TARGET_DISPLAY;
    uint32_t pw = 0, ph = 0;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            const char *n = argv[++i];
            target = -1;
            for (int k = 0; k < (int)(sizeof(kNames) / sizeof(kNames[0])); k++) {
                if (strcmp(n, kNames[k]) == 0) {
                    target = k;
                }
            }
            if (target < 0) {
                fprintf(stderr, "unknown target %s\n", n);
                return 1;
            }
        } else if (strcmp(argv[i], "--present") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%ux%u", &pw, &ph) != 2 || !pw || !ph) {
                fprintf(stderr, "bad --present size\n");
                return 1;
            }
        } else {
            fprintf(stderr, "unknown argument %s\n", argv[i]);
            return 1;
        }
    }
    uint32_t gw = 0, gh = 0;
    FILE *probe = fopen(dump, "rb");
    if (!probe) {
        /* rd_pixel writes the dump; without a Vulkan device it skipped */
        fprintf(stderr, "%s: no such dump (skipped)\n", dump);
        return 77;
    }
    fclose(probe);
    if (!peekSize(dump, &gw, &gh) || !gw || !gh || gw > 4096 || gh > 4096) {
        fprintf(stderr, "%s: not an rd dump\n", dump);
        return 1;
    }
    RdSettings s;
    memset(&s, 0, sizeof(s));
    s.preset = RD_PRESET_ORIGINAL;
    s.outputWidth = pw;
    s.outputHeight = ph;
    s.aspect = 4.0f / 3.0f;
    if (!rd_Init(gw, gh, &s, NULL)) {
        fprintf(stderr, "no usable Vulkan device\n");
        return 77;
    }
    rd__SetNotImplementedFatal(false);
    RdFrame f;
    if (!rd__LoadFrame(dump, &f)) {
        rd_Shutdown();
        return 1;
    }
    int rc = 1;
    if (rd__ReplayFrame(&f, (int)f.keep, pw != 0)) {
        uint32_t w = 0, h = 0;
        size_t cap = pw ? (size_t)pw * ph * 4 : (size_t)4096 * 4096 * 4;
        uint8_t *px = malloc(cap);
        bool ok = px && (pw ? rd__ReadPresent(px, cap, &w, &h)
                            : rd__ReadTarget(rd_Target((RdTargetId)target), px, cap, &w, &h));
        if (ok && rd_WritePng(png, px, w, h, w * 4, 1)) {
            printf("%s: frame %u, %ux%u -> %s (%u commands skipped)\n", dump, f.number, w, h, png,
                   rd__NotImplementedCount());
            rc = 0;
        } else {
            fprintf(stderr, "readback or PNG write failed\n");
        }
        free(px);
    }
    rd__FrameFree(&f);
    rd_Shutdown();
    return rc;
}
