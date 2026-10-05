/*
 * port/platform/vsync_hooks.c
 *
 * The on-vsync callback list (host_loop.h). Apart from host_loop.c so that
 * device code (package 1C's cdvd_host.c) and its tests link it without the
 * game.
 */
#include "host_loop.h"
#include "sched.h"

#define VSYNC_HOOKS 16

typedef struct VsyncHook {
    void (*fn)(void *);
    void *user;
} VsyncHook;

static VsyncHook hooks[VSYNC_HOOKS];
static int n_hooks;

int ico_host_on_vsync_register(void (*fn)(void *user), void *user)
{
    if (fn == 0 || n_hooks == VSYNC_HOOKS) {
        return -1;
    }
    hooks[n_hooks].fn = fn;
    hooks[n_hooks].user = user;
    n_hooks++;
    return 0;
}

void ico_host_on_vsync_unregister(void (*fn)(void *user), void *user)
{
    int i;
    for (i = 0; i < n_hooks; i++) {
        if (hooks[i].fn == fn && hooks[i].user == user) {
            for (; i + 1 < n_hooks; i++) {
                hooks[i] = hooks[i + 1];
            }
            n_hooks--;
            return;
        }
    }
}

void ico_host_run_vsync_hooks(void)
{
    int i;
    ico_sched_interrupt_begin();
    for (i = 0; i < n_hooks; i++) {
        hooks[i].fn(hooks[i].user);
    }
    ico_sched_interrupt_end();
}
