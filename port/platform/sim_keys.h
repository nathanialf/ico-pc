/*
 * port/platform/sim_keys.h   (CONTRACT, v0.9.6)
 *
 * The config keys that change the simulation: the pad recording's header
 * lists them (main_host.c record_keys moves here) and a save state's header
 * hashes them (xxh3-64 over "key=value\n" in this order, the value as
 * ico_config_get_string gives it; "" when unset).  A state whose hash
 * differs is refused with the first differing key named.  Add a key here
 * when an option changes what the game computes (SPEED adds
 * gameplay.shadow_speed).
 */
#ifndef ICO_PLATFORM_SIM_KEYS_H
#define ICO_PLATFORM_SIM_KEYS_H

#include <stddef.h>
#include <stdint.h>

/* NULL-terminated, "section.key" form. */
extern const char *const ICO_SIM_KEYS[];
/* The hash over the current config; also writes the "key=value\n" text into
   text (when non-NULL, size bytes, truncated) for the state header. */
uint64_t ico_sim_keys_hash(char *text, size_t size);
/* The first key whose value in `saved_text` (the header's text) differs
   from the current config; NULL when none.  out gets "key=saved (now cur)". */
const char *ico_sim_keys_first_diff(const char *saved_text, char *out, size_t size);

#endif /* ICO_PLATFORM_SIM_KEYS_H */
