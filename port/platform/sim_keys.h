/*
 * port/platform/sim_keys.h   (CONTRACT, v0.9.6)
 *
 * The config keys that change the simulation: the pad recording's header
 * lists them (main_host.c record_keys moves here) and a save state's header
 * hashes them (xxh3-64 over "key=value\n" in this order, the value as
 * ico_config_get_string gives it; "" when unset).  A state is never refused
 * for a key: it loads under this run's settings, the keys that differ named
 * in one log line (state_file.h ico_state_keys_diff) and each key's carry
 * (below) saying what the load does about it.  Add a key here when an
 * option changes what the game computes (DOOR added gameplay.door_fix),
 * with its default (sim_keys.c kDefaults: a state saved before the key
 * existed lacks it and reads as that default, through
 * ico_state_set_key_default) and its carry (sim_keys.c kCarry).
 */
#ifndef ICO_PLATFORM_SIM_KEYS_H
#define ICO_PLATFORM_SIM_KEYS_H

#include <stddef.h>
#include <stdint.h>

/* NULL-terminated, "section.key" form. */
extern const char *const ICO_SIM_KEYS[];
/* The keys that left the list in v0.11.3 (the fifteen difficulty rows, now
   each save's record, port/game/difficulty.h, carried by a state as its own
   block): NULL-terminated.  A state's keys text may still list them (a state
   of v0.11.2 or older); the key check skips them. */
extern const char *const ICO_SIM_KEYS_RETIRED[];
/* 1 when the key (key_n bytes, not NUL-terminated) is in
   ICO_SIM_KEYS_RETIRED. */
int ico_sim_key_retired(const char *key, size_t key_n);
/* The hash over the current config; also writes the "key=value\n" text into
   text (when non-NULL, size bytes, truncated) for the state header. */
uint64_t ico_sim_keys_hash(char *text, size_t size);
/* The first key whose value in `saved_text` (the header's text) differs
   from the current config; NULL when none.  out gets "key=saved (now cur)".
   A key the saved text lacks reads as its default: equal while the key is
   unset or at its default now, else "key=(absent) (now cur)".  A key unset
   on one side and at its default's text on the other is equal. */
const char *ico_sim_keys_first_diff(const char *saved_text, char *out, size_t size);
/* key's value in a "key=value\n" text (a state header's keys) into out
   (size bytes, cut, NUL-terminated): 1, or 0 when the text does not list
   the key (out ""). */
int ico_sim_keys_value(const char *text, const char *key, char *out, size_t size);
/* A key's default as the config text gives it ("false", "original"), NULL
   for a key without one fixed text (unset is the disc's or the device's
   own) or a key not in the list. */
const char *ico_sim_key_default(const char *key);
/* 1 when `value` (value_n bytes) means the key's default: empty (unset) or
   its default text; 0 otherwise and for a key not in the list.  The key is
   key_n bytes (not NUL-terminated); state_file.h IcoStateKeyDefaultFn. */
int ico_sim_key_is_default(const char *key, size_t key_n, const char *value, size_t value_n);

/* What a load does about a key whose value in the state is not this run's
   (state_host.c perform_load: the state loads under this run's settings).
   Every key of the list declares one (sim_keys.c kCarry, beside kDefaults,
   the same size rule). */
typedef enum IcoSimKeyCarry {
    /* nothing to do: read live where it is used, or only at the start, or
       a latch of the port's that clears itself (the comment at each key
       says which) */
    ICO_SIM_CARRY_HARMLESS = 0,
    /* the restore put the state's value into the game or a port block: the
       load puts this run's back and logs it (ico_variants_state_loaded,
       ico_door_grace_state_loaded, ui_settings_reapply_after_state_load) */
    ICO_SIM_CARRY_FIXUP,
    /* the state's own stays for this session (video.video_mode: the 50/60 Hz
       mode the state runs in); the setting applies from the next start */
    ICO_SIM_CARRY_KEPT
} IcoSimKeyCarry;

/* key's carry, or -1 for a key not in the list */
int ico_sim_key_carry(const char *key);
/* "harmless", "fix-up", "kept" ("?" for another value) */
const char *ico_sim_key_carry_name(int carry);

#endif /* ICO_PLATFORM_SIM_KEYS_H */
