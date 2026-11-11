/* rd_made_outside.c: what is made in play outside the renderer's key cache,
 * named (rd_made_outside.h). */
#include "rd_made_outside.h"

#include <stdio.h>
#include <string.h>

#include "rd_internal.h"

/* the names seen in play, each with its count; past RD_MADE_NAMES the
   last entry is the shared one ("other names") */
typedef struct MadeName {
    int kind;
    char name[48];
    uint64_t count;
} MadeName;

static struct {
    bool hooked;
    int watch;         /* rd_made_outside_watch: play has begun (an atomic word) */
    int key;           /* inside a key cache creation (an atomic count) */
    char lock;         /* the table's (an atomic flag: creations on two threads) */
    uint32_t used;     /* entries of names[] */
    uint64_t kinds[3]; /* by kind: the creations */
    uint32_t kindNames[3];
    MadeName names[RD_MADE_NAMES];
} s_made;

void rd__made_outside_key_begin(void)
{
    __atomic_add_fetch(&s_made.key, 1, __ATOMIC_ACQ_REL);
}

void rd__made_outside_key_end(void)
{
    __atomic_sub_fetch(&s_made.key, 1, __ATOMIC_ACQ_REL);
}

static void where(char *out, size_t size)
{
    const int list = rd__replay_list();
    if (list >= 0) {
        snprintf(out, size, "list %d", list);
    } else if (rd__vu_prepass_running()) {
        snprintf(out, size, "the vertex pre-pass");
    } else {
        snprintf(out, size, "no draw list: the present, a check or the game thread");
    }
}

static void hook(int kind, const char *name, uint64_t bytes)
{
    if (!__atomic_load_n(&s_made.watch, __ATOMIC_ACQUIRE) || kind < 1 || kind > 2) {
        return;
    }
    if (kind == RHI_CREATE_PIPELINE && __atomic_load_n(&s_made.key, __ATOMIC_ACQUIRE) > 0) {
        return; /* the key cache's own: firstUseLine names it */
    }
    const char *nm = name != NULL && name[0] != '\0' ? name : "unnamed";
    while (__atomic_test_and_set(&s_made.lock, __ATOMIC_ACQUIRE)) {}
    MadeName *e = NULL;
    for (uint32_t i = 0; i < s_made.used && !e; i++) {
        if (s_made.names[i].kind == kind && strcmp(s_made.names[i].name, nm) == 0) {
            e = &s_made.names[i];
        }
    }
    if (!e && s_made.used < RD_MADE_NAMES) {
        e = &s_made.names[s_made.used++];
        e->kind = kind;
        snprintf(e->name, sizeof(e->name), "%s", nm);
        s_made.kindNames[kind]++;
    } else if (!e) {
        e = &s_made.names[RD_MADE_NAMES - 1u]; /* the table full: the last name's count */
    }
    const uint64_t nth = ++e->count;
    const uint64_t kc = ++s_made.kinds[kind];
    const uint32_t kn = s_made.kindNames[kind];
    __atomic_clear(&s_made.lock, __ATOMIC_RELEASE);
    if (!rd_made_outside_due(nth)) {
        return;
    }
    char w[64], line[384];
    where(w, sizeof(w));
    (void)rd_made_outside_text(line, sizeof(line), kind, nm, bytes, g_rd.replayCounter, w, nth, kc,
                               kn);
    rd__log("%s", line);
}

void rd__made_outside_init(void)
{
    rhi_set_create_hook(hook);
    s_made.hooked = true;
}

void rd_made_outside_watch(void)
{
    if (!s_made.hooked || __atomic_load_n(&s_made.watch, __ATOMIC_ACQUIRE)) {
        return;
    }
    __atomic_store_n(&s_made.watch, 1, __ATOMIC_RELEASE);
    rd__log("made in play outside the key cache: watched from here (every pipeline made one at a "
            "time outside it and every buffer, by name: the first of each name, then its first 8 "
            "and every 100th; the run's end line counts them)");
}

void rd__made_outside_shutdown(void)
{
    if (s_made.hooked) {
        rhi_set_create_hook(NULL);
    }
    if (s_made.kinds[1] + s_made.kinds[2] != 0u) {
        char list[1024];
        size_t at = 0;
        list[0] = '\0';
        for (uint32_t i = 0; i < s_made.used && at < sizeof(list); i++) {
            const MadeName *e = &s_made.names[i];
            const int n = snprintf(list + at, sizeof(list) - at, "%s%s \"%s\" %llu", at ? ", " : "",
                                   e->kind == RHI_CREATE_PIPELINE ? "pipeline" : "buffer", e->name,
                                   (unsigned long long)e->count);
            at += n > 0 ? (size_t)n : 0u;
        }
        rd__log("made in play outside the key cache, at the end of the run: %llu pipelines (%u "
                "names), %llu buffers (%u names): %s%s",
                (unsigned long long)s_made.kinds[1], s_made.kindNames[1],
                (unsigned long long)s_made.kinds[2], s_made.kindNames[2], list,
                s_made.used == RD_MADE_NAMES ? " (the last also counts every name past the table)"
                                             : "");
    }
    memset(&s_made, 0, sizeof(s_made));
}
