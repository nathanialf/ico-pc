/* texpack_e2e_test: the texture pack end to end on the real pieces.
 *
 *   texpack_e2e_test NAMES TEXTURES WORKDIR
 *
 * NAMES is texpack_disc_test's texpack_names.txt (every texture of the
 * disc with its candidate names, as the cache's hook computes them);
 * TEXTURES a folder laid out as a "textures" folder beside the program
 * (it holds SCES-50760/replacements, e.g. build-host/tmp/texpack).  WORKDIR
 * gets a program folder whose "textures" links to TEXTURES, which
 * texpack_Init walks as the game does.  Every texture's candidates are
 * looked up in that index in the hook's order (the first hit is the one
 * requested), and every file reached is read with the real loaders.
 * Exit 77 without the names file or the pack.  Fails when no texture
 * reaches a file or a reached file does not load.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host_fs.h"
#include "texpack.h"

#ifndef _WIN32
#include <unistd.h>
#endif

static int readAll(const char *path, uint8_t **data, size_t *size)
{
    FILE *f = ico_fopen(path, "rb");
    long n;
    *data = NULL;
    if (!f || fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) != 0) {
        if (f) {
            fclose(f);
        }
        return -1;
    }
    *data = malloc((size_t)n);
    if (!*data || fread(*data, 1, (size_t)n, f) != (size_t)n) {
        free(*data);
        fclose(f);
        return -1;
    }
    fclose(f);
    *size = (size_t)n;
    return 0;
}

static int hasExt(const char *p, const char *ext)
{
    size_t n = strlen(p), e = strlen(ext);
    if (n < e) {
        return 0;
    }
    for (size_t i = 0; i < e; i++) {
        char c = p[n - e + i];
        if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != ext[i]) {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: texpack_e2e_test NAMES TEXTURES WORKDIR\n");
        return 2;
    }
    char repl[2048], prog[2048];
    snprintf(repl, sizeof(repl), "%s/SCES-50760/replacements", argv[2]);
    FILE *names = ico_fopen(argv[1], "rb");
    if (!names || ico_path_kind(repl, NULL, NULL) != 1) {
        printf("texpack_e2e_test: no names file (%s) or no pack (%s): skipped\n", argv[1], repl);
        if (names) {
            fclose(names);
        }
        return 77;
    }
#ifdef _WIN32
    printf("texpack_e2e_test: needs a folder link: skipped on Windows\n");
    fclose(names);
    return 77;
#else
    char link[2100];
    snprintf(prog, sizeof(prog), "%s/texpack_e2e_prog", argv[3]);
    snprintf(link, sizeof(link), "%s/textures", prog);
    (void)ico_mkdir(prog);
    (void)remove(link);
    if (symlink(argv[2], link) != 0) {
        printf("texpack_e2e_test: cannot link %s: skipped\n", link);
        fclose(names);
        return 77;
    }
#endif
    TexpackConfig c;
    memset(&c, 0, sizeof(c));
    c.programDir = prog;
    c.serial = "SCES-50760";
    c.bcSupported = 1;
    const int indexed = texpack_Init(&c);
    TexpackStats st;
    texpack_GetStats(&st);
    printf("texpack_e2e_test: %d replacements indexed (%u files, %u duplicates, %u malformed)\n",
           indexed, st.files, st.duplicates, st.malformed);

    char *hit = calloc((size_t)(indexed > 0 ? st.files : 1) + 1, 1);
    static char line[1 << 16];
    int textures = 0, reached = 0, refused = 0, firstNotSingle = 0;
    while (fgets(line, sizeof(line), names)) {
        char *colon = strstr(line, " : ");
        if (!colon) {
            continue;
        }
        textures++;
        if (strncmp(colon + 3, "refused", 7) == 0) {
            refused++;
            continue;
        }
        /* the candidates in the hook's order: name@level[/chain] */
        for (char *tok = strtok(colon + 3, " \r\n"); tok; tok = strtok(NULL, " \r\n")) {
            char file[128];
            char *at = strchr(tok, '@');
            TexpackName tn;
            if (!at || (size_t)(at - tok) + 5 > sizeof(file)) {
                continue;
            }
            snprintf(file, sizeof(file), "%.*s.png", (int)(at - tok), tok);
            if (texpack_ParseName(file, &tn) != 0) {
                continue;
            }
            int e = texpack_Lookup(&tn);
            if (e >= 0) {
                reached++;
                if (e <= (int)st.files) {
                    hit[e] = 1;
                }
                /* "@0" alone: the bound base level, the reference pack's */
                firstNotSingle += strcmp(at, "@0") != 0;
                break;
            }
        }
    }
    fclose(names);

    int entries = 0, loaded = 0, failed = 0;
    for (int e = 0; e <= (int)st.files; e++) {
        if (!hit[e]) {
            continue;
        }
        entries++;
        const char *path = texpack_EntryPath(e);
        uint8_t *data;
        size_t size;
        TexpackImage img;
        int r = -1;
        if (path && readAll(path, &data, &size) == 0) {
            r = hasExt(path, ".dds") ? texpack_LoadDds(data, size, 1, path, &img)
                                     : texpack_LoadPng(data, size, path, &img);
            free(data);
        }
        if (r == 0 && img.w > 0 && img.h > 0 && img.levels > 0) {
            loaded++;
            texpack_FreeImage(&img);
        } else {
            failed++;
            printf("texpack_e2e_test: %s does not load\n", path ? path : "?");
        }
    }
    free(hit);
    texpack_Shutdown();
    printf("texpack_e2e_test: %d textures (%d refused); %d reach a file (%d through a name other "
           "than the base level alone); %d of %d indexed files reached, %d loaded, %d failed\n",
           textures, refused, reached, firstNotSingle, entries, indexed, loaded, failed);
    if (reached == 0 || failed > 0) {
        printf("texpack_e2e_test: FAIL\n");
        return 1;
    }
    printf("texpack_e2e_test: ok\n");
    return 0;
}
