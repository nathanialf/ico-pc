/*
 * port/platform/state_registry.c
 *
 * The table of port globals a save state carries (state_registry.h).
 * The owners register their blocks once at start-up (state_port.h lists
 * them; ico_state_register_port calls them in a fixed order), so the
 * table's order, names and sizes are the same in every run of one build:
 * they go into the build id (state_file.h).
 */
#include "state_registry.h"

#include <stdio.h>
#include <string.h>

static IcoStateEntry table[ICO_STATE_MAX_ENTRIES];
static int count;

int ico_state_register(const char *name, void *p, size_t n, unsigned flags)
{
    int i;

    if (name == NULL || name[0] == '\0' || p == NULL || n == 0) {
        fprintf(stderr, "state: a block without a name, address or size was not registered\n");
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (strcmp(table[i].name, name) == 0) {
            /* the same block twice (an owner's init run again) is quiet */
            if (table[i].p != p || table[i].n != n || table[i].flags != flags) {
                fprintf(stderr, "state: the name %s is registered already\n", name);
            }
            return -1;
        }
    }
    if (count == ICO_STATE_MAX_ENTRIES) {
        fprintf(stderr, "state: the registry is full (%d blocks); %s was not registered\n",
                ICO_STATE_MAX_ENTRIES, name);
        return -1;
    }
    table[count].name = name;
    table[count].p = p;
    table[count].n = n;
    table[count].flags = flags;
    count++;
    return 0;
}

int ico_state_registry_count(void)
{
    return count;
}

const IcoStateEntry *ico_state_registry_entry(int i)
{
    return i >= 0 && i < count ? &table[i] : NULL;
}

void ico_state_registry_reset(void)
{
    memset(table, 0, sizeof table);
    count = 0;
}
