/* gl_loader.c: the OpenGL entry points of the OpenGL backend (gl_loader.h),
 * fetched with SDL_GL_GetProcAddress; no system GL library is linked. */
#include "gl_loader.h"
#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

GlFuncs rhi_gl_funcs;

typedef struct GlEntry {
    const char *name; /* "glName" */
    size_t offset;    /* of the member in GlFuncs */
    bool required;    /* OpenGL 4.3 core */
} GlEntry;

#define GL__ENTRY(req, type, n) {"gl" #n, offsetof(GlFuncs, n), (req) != 0},

static const GlEntry s_entries[] = {GL_FUNCS(GL__ENTRY)};

#undef GL__ENTRY

unsigned gl_loader_count(void)
{
    return (unsigned)(sizeof(s_entries) / sizeof(s_entries[0]));
}

bool gl_loader_load(unsigned *optionalMissing)
{
    unsigned missing = 0;
    memset(&rhi_gl_funcs, 0, sizeof(rhi_gl_funcs));
    for (unsigned i = 0; i < gl_loader_count(); i++) {
        const GlEntry *e = &s_entries[i];
        SDL_FunctionPointer p = SDL_GL_GetProcAddress(e->name);
        if (!p) {
            if (e->required) {
                fprintf(stderr, "rhi_gl: the driver has no %s (OpenGL 4.3 core)\n", e->name);
                gl_loader_clear();
                return false;
            }
            missing++;
            continue;
        }
        /* every member is a function pointer of the same size; the
         * pointer's bits go in as they are */
        memcpy((char *)&rhi_gl_funcs + e->offset, &p, sizeof(p));
    }
    if (optionalMissing) {
        *optionalMissing = missing;
    }
    return true;
}

void gl_loader_clear(void)
{
    memset(&rhi_gl_funcs, 0, sizeof(rhi_gl_funcs));
}
