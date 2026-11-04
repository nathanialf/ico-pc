/* gl_compile.c: the OpenGL backend's shader compiles (gl_internal.h
 * gl_compile_init, gl_compile_program, gl_compile_shutdown).
 *
 * The rc2 report: Options > Graphics > Apply to OpenGL on an RTX 3090
 * (driver 610.88) left the game thread inside NVIDIA's GLSL compiler under
 * glCreateShaderProgramv (sprite_aa1_ps, rd_replay.c's start-up shaders) for
 * as long as the player waited; nothing took the window's events meanwhile,
 * so Windows showed it black and "Not Responding".  OpenGL compiles a
 * program on the thread that asks for it, so the compile moves to a thread
 * of its own when the window set its wait hooks (rhi.h RhiWaitHooks):
 *
 *  - gl_compile_init (the end of rhi_init, the device's context current on
 *    the window): a second context made with SDL_GL_SHARE_WITH_CURRENT_CONTEXT
 *    (the same share group: a program it makes is the device's), the
 *    device's context made current again, and a thread that makes the new
 *    context current, without a surface where SDL allows it (EGL) and on the
 *    game's window otherwise (WGL, GLX: a drawable may be current on two
 *    threads at once with two contexts).  The thread keeps its own copies of
 *    the five entry points it calls, since rhi_shutdown clears glf.
 *  - gl_compile_program: the text handed to the thread, which makes the
 *    program, reads its link status (the compile's end) and its log, and
 *    glFinish-es so the program is complete before the device's context
 *    uses it; the calling thread waits in 50 ms slices and runs the hooks'
 *    pump between them (the window takes its events: port/platform/
 *    window_switch.c), with a direct line once a compile passes 2 s.
 *  - The limit (RhiWaitHooks.limitNs, the GPU hang limit, 15 s by default):
 *    a compile that has not returned by then is abandoned.  The thread is
 *    left inside the driver: a thread blocked in a driver call cannot be
 *    stopped safely.  Its context is kept (never destroyed while it may be
 *    current there), its state block is never freed, and the OpenGL library
 *    is loaded once more (SDL_GL_LoadLibrary: SDL unloads it when the last
 *    OpenGL window goes, which would unmap the code the thread runs in).
 *    Should the compile return later, the thread deletes the program, logs
 *    the time and ends.  The device's start fails (every later compile
 *    returns 0 at once), the window's stalled hook says which shader, and
 *    the switch goes back to the backend before with a box.
 *
 * Without the hooks (the tests, the replay tool) or with ICO_GL_COMPILE_THREAD=0
 * the thread is not made; KHR_parallel_shader_compile, where the driver
 * offers it and the hooks are set, then lets the calling thread poll
 * GL_COMPLETION_STATUS_KHR with the same pump and limit (a driver that
 * compiles inside glCreateShaderProgramv anyway still blocks); without either
 * the compile blocks as before. */
#include "gl_internal.h"
#include <stdlib.h>
#include <string.h>

/* the compile thread's block: the device's g_gl.compiler while it serves
 * the device; an abandoned one is left to its thread (never freed) */
typedef struct GlCompiler {
    SDL_Thread *thread;
    SDL_GLContext ctx;
    SDL_Window *window; /* the drawable it is current on without a surface */
    SDL_Mutex *lock;
    SDL_Condition *cond;
    /* its own entry points (glf is cleared at rhi_shutdown) */
    PFNGLCREATESHADERPROGRAMVPROC createShaderProgramv;
    PFNGLGETPROGRAMIVPROC getProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC getProgramInfoLog;
    PFNGLDELETEPROGRAMPROC deleteProgram;
    PFNGLFINISHPROC finish;
    /* under lock */
    int up;         /* 0 starting, 1 its context current, -1 it could not be */
    bool noSurface; /* current without a surface */
    int state;      /* GL_COMPILE_* */
    bool abandoned;
    GLenum type;
    char *text;
    char name[96];
    uint64_t postedNs;
    GLuint program;
    GLint linked;
    char log[2048];
} GlCompiler;

enum { GL_COMPILE_IDLE, GL_COMPILE_POSTED, GL_COMPILE_DONE, GL_COMPILE_QUIT };

/* a compile this long writes its line at once */
#define GL_COMPILE_SLOW_NS 2000000000ull
/* the calling thread's wait slice (the window's pump between two) */
#define GL_COMPILE_SLICE_MS 50
/* the compile thread's context current on it within this */
#define GL_COMPILE_UP_MS 5000ull

static int SDLCALL gl_compile_main(void *arg)
{
    GlCompiler *c = arg;
    rhi_thread_begin(); /* the process's cores (rhi.h rhi_set_thread_begin) */
    /* without a surface where SDL allows it (EGL with
       KHR_surfaceless_context), else on the game's window */
    bool noSurface = SDL_GL_MakeCurrent(NULL, c->ctx);
    const bool up = noSurface || SDL_GL_MakeCurrent(c->window, c->ctx);

    SDL_LockMutex(c->lock);
    c->up = up ? 1 : -1;
    c->noSurface = noSurface;
    SDL_BroadcastCondition(c->cond);
    if (!up || c->abandoned) {
        /* not current, or the device gave up waiting for it (its block
           and context are left to this thread) */
        SDL_UnlockMutex(c->lock);
        if (up) {
            SDL_GL_MakeCurrent(NULL, NULL);
        }
        return 0;
    }
    for (;;) {
        while (c->state != GL_COMPILE_POSTED && c->state != GL_COMPILE_QUIT) {
            SDL_WaitCondition(c->cond, c->lock);
        }
        if (c->state == GL_COMPILE_QUIT) {
            break;
        }
        const GLenum type = c->type;
        char *text = c->text;
        char name[sizeof(c->name)];
        memcpy(name, c->name, sizeof(name));
        const uint64_t posted = c->postedNs;
        c->text = NULL;
        SDL_UnlockMutex(c->lock);

        const GLchar *src = text;
        const GLuint program = c->createShaderProgramv(type, 1, &src);
        GLint linked = GL_FALSE;
        char log[sizeof(c->log)];
        log[0] = '\0';
        if (program) {
            c->getProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked != GL_TRUE) {
                GLsizei n = 0;
                c->getProgramInfoLog(program, (GLsizei)sizeof(log), &n, log);
                log[n > 0 && n < (GLsizei)sizeof(log) ? n : 0] = '\0';
            }
        }
        /* the program complete before another context of the group uses it */
        c->finish();
        free(text);

        SDL_LockMutex(c->lock);
        if (c->abandoned) {
            /* the caller gave up on it: nobody takes the program; the
               thread ends, its context and block are left as they are */
            if (program) {
                c->deleteProgram(program);
            }
            SDL_UnlockMutex(c->lock);
            fprintf(stderr,
                    "rhi_gl: the abandoned compile of %s returned after %.1f s; its thread "
                    "ends\n",
                    name, (double)(SDL_GetTicksNS() - posted) / 1e9);
            fflush(stderr);
            return 0;
        }
        c->program = program;
        c->linked = linked;
        memcpy(c->log, log, sizeof(c->log));
        c->state = GL_COMPILE_DONE;
        SDL_BroadcastCondition(c->cond);
    }
    SDL_UnlockMutex(c->lock);
    SDL_GL_MakeCurrent(NULL, NULL);
    return 0;
}

static void gl_compile_free(GlCompiler *c)
{
    if (c->ctx) {
        SDL_GL_DestroyContext(c->ctx);
    }
    if (c->cond) {
        SDL_DestroyCondition(c->cond);
    }
    if (c->lock) {
        SDL_DestroyMutex(c->lock);
    }
    free(c);
}

/* the compile thread, NULL (and why) when it cannot be made */
static GlCompiler *gl_compile_thread_make(const char **why)
{
    GlCompiler *c = calloc(1, sizeof(*c));
    if (!c) {
        *why = "out of memory";
        return NULL;
    }
    c->window = g_gl.window;
    c->createShaderProgramv = glf.CreateShaderProgramv;
    c->getProgramiv = glf.GetProgramiv;
    c->getProgramInfoLog = glf.GetProgramInfoLog;
    c->deleteProgram = glf.DeleteProgram;
    c->finish = glf.Finish;
    c->lock = SDL_CreateMutex();
    c->cond = SDL_CreateCondition();
    if (!c->lock || !c->cond) {
        *why = "no mutex";
        gl_compile_free(c);
        return NULL;
    }
    /* a context of the device's share group; SDL makes it current here,
       so the device's own is made current again at once */
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1);
    c->ctx = SDL_GL_CreateContext(g_gl.window);
    SDL_GL_SetAttribute(SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0);
    if (!SDL_GL_MakeCurrent(g_gl.window, g_gl.context)) {
        GL_LOG_DIRECT("the device's context is not current again after the compile thread's "
                      "(%s)",
                      SDL_GetError());
    }
    if (!c->ctx) {
        *why = SDL_GetError();
        gl_compile_free(c);
        return NULL;
    }
    c->thread = SDL_CreateThread(gl_compile_main, "ico_gl_compile", c);
    if (!c->thread) {
        *why = SDL_GetError();
        gl_compile_free(c);
        return NULL;
    }
    /* its context current there within GL_COMPILE_UP_MS, else the thread
       is left to itself (a driver call that does not return) */
    const uint64_t t0 = gl_now_ns();
    SDL_LockMutex(c->lock);
    while (c->up == 0 && gl_now_ns() - t0 < GL_COMPILE_UP_MS * 1000000ull) {
        (void)SDL_WaitConditionTimeout(c->cond, c->lock, GL_COMPILE_SLICE_MS);
    }
    const int up = c->up;
    if (up == 0) {
        c->abandoned = true;
    }
    SDL_UnlockMutex(c->lock);
    if (up == 0) {
        *why = "its context did not become current on the thread in 5 s (the thread is left)";
        SDL_DetachThread(c->thread);
        return NULL;
    }
    if (up < 0) {
        *why = "its context could not be made current on the thread";
        SDL_WaitThread(c->thread, NULL);
        gl_compile_free(c);
        return NULL;
    }
    return c;
}

void gl_compile_init(void)
{
    g_gl.compiler = NULL;
    g_gl.parallelKhr = false;
    if (!g_gl.hasWaits || !g_gl.window) {
        return; /* the tests and tools: the calling thread compiles, no line */
    }
    const double limitS = (double)g_gl.waits.limitNs / 1e9;
    const char *env = getenv("ICO_GL_COMPILE_THREAD");
    const char *why = "ICO_GL_COMPILE_THREAD=0";
    if (!(env && env[0] == '0')) {
        g_gl.compiler = gl_compile_thread_make(&why);
    }
    if (g_gl.compiler) {
        GL_LOG_DIRECT("shaders: compiled on a thread of their own (a context of the same share "
                      "group, %s); the window takes its events meanwhile; one compile may take "
                      "%.0f s (%s), past it OpenGL does not start",
                      g_gl.compiler->noSurface ? "without a surface" : "on the window", limitS,
                      limitS > 0.0 ? "the GPU hang limit" : "no limit");
        return;
    }
    g_gl.parallelKhr =
        glf.MaxShaderCompilerThreadsKHR && (gl_has_extension("GL_KHR_parallel_shader_compile") ||
                                            gl_has_extension("GL_ARB_parallel_shader_compile"));
    if (g_gl.parallelKhr) {
        glf.MaxShaderCompilerThreadsKHR(0xFFFFFFFFu);
    }
    GL_LOG_DIRECT("shaders: no compile thread (%s); %s", why ? why : "?",
                  g_gl.parallelKhr ? "the driver's parallel compile (KHR_parallel_shader_compile) "
                                     "is polled with the window's events between, the same limit"
                                   : "each compile blocks the window until it returns");
}

void gl_compile_shutdown(void)
{
    GlCompiler *c = g_gl.compiler;
    if (g_gl.compiles) {
        GL_LOG("shaders: %u compiled in %.1f s, the longest %s %.0f ms%s", g_gl.compiles,
               (double)g_gl.compileNs / 1e9,
               g_gl.compileLongestName[0] ? g_gl.compileLongestName : "?",
               (double)g_gl.compileLongestNs / 1e6, c ? " (on the compile thread)" : "");
    }
    if (!c) {
        return;
    }
    g_gl.compiler = NULL;
    SDL_LockMutex(c->lock);
    c->state = GL_COMPILE_QUIT;
    SDL_BroadcastCondition(c->cond);
    SDL_UnlockMutex(c->lock);
    SDL_WaitThread(c->thread, NULL);
    gl_compile_free(c); /* the thread released its context before it ended */
}

/* a compile past the limit: given up on (the file's header says how) */
static void gl_compile_abandon(const char *name, uint64_t heldNs, const char *where)
{
    g_gl.compileStalled = true;
    snprintf(g_gl.stalledName, sizeof(g_gl.stalledName), "%s", name);
    /* SDL unloads the OpenGL library with the last OpenGL window; the
       thread left inside the driver must keep its code mapped */
    if (!SDL_GL_LoadLibrary(NULL)) {
        GL_LOG_DIRECT("the OpenGL library could not be held loaded (%s)", SDL_GetError());
    }
    GL_LOG_DIRECT("shader %s: the graphics driver has compiled it for %.1f s, past the limit of "
                  "%.1f s (the GPU hang limit): the compile is abandoned %s, and OpenGL does not "
                  "start",
                  name, (double)heldNs / 1e9, (double)g_gl.waits.limitNs / 1e9, where);
    if (g_gl.waits.stalled) {
        g_gl.waits.stalled(g_gl.waits.ctx, name, heldNs);
    }
}

/* between two slices: the slow line once, the window's pump */
static void gl_compile_tick(const char *name, uint64_t heldNs, bool *said)
{
    if (!*said && heldNs >= GL_COMPILE_SLOW_NS) {
        *said = true;
        GL_LOG_DIRECT("shader %s: compiling for %.1f s (the window takes its events meanwhile; "
                      "the limit %.0f s)",
                      name, (double)heldNs / 1e9, (double)g_gl.waits.limitNs / 1e9);
    }
    if (g_gl.waits.pump) {
        char what[128];
        snprintf(what, sizeof(what), "shader %s", name);
        g_gl.waits.pump(g_gl.waits.ctx, what, heldNs);
    }
    gl_start_screen_wait(); /* the progress screen moves (gl_start_screen.c) */
}

/* the link status and log of a program made on this thread */
static void gl_compile_status(GLuint program, GLint *linked, char *log, size_t logSize)
{
    glf.GetProgramiv(program, GL_LINK_STATUS, linked);
    if (*linked != GL_TRUE && logSize > 0) {
        GLsizei n = 0;
        glf.GetProgramInfoLog(program, (GLsizei)logSize, &n, log);
        log[n > 0 && (size_t)n < logSize ? n : 0] = '\0';
    }
}

/* on the compile thread; 0 when it was abandoned */
static GLuint gl_compile_on_thread(GlCompiler *c, GLenum type, char *text, const char *name,
                                   GLint *linked, char *log, size_t logSize)
{
    const uint64_t t0 = gl_now_ns();
    const uint64_t limit = g_gl.waits.limitNs;
    bool said = false;
    GLuint program = 0;

    SDL_LockMutex(c->lock);
    c->type = type;
    c->text = text;
    snprintf(c->name, sizeof(c->name), "%s", name);
    c->postedNs = t0;
    c->state = GL_COMPILE_POSTED;
    SDL_BroadcastCondition(c->cond);
    for (;;) {
        if (c->state != GL_COMPILE_DONE) {
            (void)SDL_WaitConditionTimeout(c->cond, c->lock, GL_COMPILE_SLICE_MS);
        }
        if (c->state == GL_COMPILE_DONE) {
            break;
        }
        const uint64_t held = gl_now_ns() - t0;
        if (limit && held >= limit) {
            /* given up under the lock: the thread sees it when it returns */
            c->abandoned = true;
            SDL_UnlockMutex(c->lock);
            SDL_DetachThread(c->thread); /* its end, if it comes, frees the thread */
            g_gl.compiler = NULL;        /* left to its thread (rhi_shutdown does not wait) */
            gl_compile_abandon(name, held,
                               "on its thread (left inside the driver; its context and the "
                               "OpenGL library are kept for the rest of the run)");
            return 0;
        }
        SDL_UnlockMutex(c->lock);
        gl_compile_tick(name, held, &said);
        SDL_LockMutex(c->lock);
    }
    program = c->program;
    *linked = c->linked;
    snprintf(log, logSize, "%s", c->log);
    c->program = 0;
    c->state = GL_COMPILE_IDLE;
    SDL_UnlockMutex(c->lock);
    return program;
}

/* on this thread with KHR_parallel_shader_compile: polled */
static GLuint gl_compile_polled(GLenum type, char *text, const char *name, GLint *linked, char *log,
                                size_t logSize)
{
    const uint64_t t0 = gl_now_ns();
    const uint64_t limit = g_gl.waits.limitNs;
    const GLchar *src = text;
    bool said = false;
    const GLuint program = glf.CreateShaderProgramv(type, 1, &src);
    free(text);
    if (!program) {
        return 0;
    }
    for (;;) {
        GLint done = GL_FALSE;
        glf.GetProgramiv(program, GL_COMPLETION_STATUS_KHR, &done);
        if (done) {
            break;
        }
        const uint64_t held = gl_now_ns() - t0;
        if (limit && held >= limit) {
            glf.DeleteProgram(program);
            gl_compile_abandon(name, held, "(the program deleted)");
            return 0;
        }
        gl_compile_tick(name, held, &said);
        SDL_DelayNS(2000000ull);
    }
    gl_compile_status(program, linked, log, logSize);
    return program;
}

GLuint gl_compile_program(GLenum type, char *text, const char *name, GLint *linked, char *log,
                          size_t logSize)
{
    *linked = GL_FALSE;
    if (logSize > 0) {
        log[0] = '\0';
    }
    if (g_gl.compileStalled) {
        free(text); /* the device's start has failed already */
        return 0;
    }
    const uint64_t t0 = gl_now_ns();
    GLuint program;
    if (g_gl.compiler) {
        program = gl_compile_on_thread(g_gl.compiler, type, text, name, linked, log, logSize);
    } else if (g_gl.parallelKhr) {
        program = gl_compile_polled(type, text, name, linked, log, logSize);
    } else {
        const GLchar *src = text;
        program = glf.CreateShaderProgramv(type, 1, &src);
        free(text);
        if (program) {
            gl_compile_status(program, linked, log, logSize);
        }
    }
    if (!g_gl.compileStalled) {
        const uint64_t ns = gl_now_ns() - t0;
        g_gl.compiles++;
        g_gl.compileNs += ns;
        if (ns > g_gl.compileLongestNs) {
            g_gl.compileLongestNs = ns;
            snprintf(g_gl.compileLongestName, sizeof(g_gl.compileLongestName), "%s", name);
        }
        if (ns >= GL_COMPILE_SLOW_NS) {
            GL_LOG_DIRECT("shader %s: compiled in %.1f s", name, (double)ns / 1e9);
        }
    }
    return program;
}
