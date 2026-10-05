# RetroAchievements feasibility

A research note, kept as the reason the port has built-in achievements
(docs/port/ACHIEVEMENTS.md). Checked 2026-10-05. Conclusion first: RetroAchievements
(RA) will not accept a set for this port under its current policy, and the
existing ICO set's logic could not be read without an RA login. The plan's
port-owned achievement system stays the design; a retail-address memory
view is feasible for fixed globals but not for heap pointer chains in the
64-bit build, so it should not be built now.

## 1. Policy

Source: https://docs.retroachievements.org/general/standalone-support.html,
whose Markdown source is `docs/general/standalone-support.md` in
https://github.com/RetroAchievements/docs (cloned 2026-10-05).

- Line 22: "Decompilations, recompilations, and other unofficial ports are
  not eligible for standalone sets." This line came in with commit
  `12098b6`, 2026-06-29, "Update standalone requirements following from
  internal discussion (#395)" (`git log -S"Decompilations"` on that file).
  Before that date the page did not exclude decomps explicitly.
- Other requirements (same file, lines 8-21, 40): admin (RAdmin) approval
  even when every guideline is met; a full set plan; a proposer who is an
  RA Developer, or a Junior Developer with sponsorship; games at least 10
  years old with no non-bugfix updates in 5 years; enforcement of
  Hardcore restrictions (blocking cheats and mods that make achievements
  easier, per the Global Leaderboard and Achievement Hunting Rules); use of
  the Connect API
  (https://api-docs.retroachievements.org/connect/standalone.html).
- RA forum threads asking the same question (titles from a web search;
  the pages return HTTP 403 to non-browser clients, so their contents were
  not read and are not quoted): "Decomp Games Support Possible?"
  (https://retroachievements.org/forums/topic/33378), "decompilations and
  cheevos? is that a future possibility?"
  (https://retroachievements.org/forums/topic/29978), "PC Recomps"
  (https://retroachievements.org/forums/topic/36345), "Unofficial PC ports
  and retroachievements" (https://retroachievements.org/forums/topic/37340),
  "I'd love Recomp/Decomp support"
  (https://retroachievements.org/forums/topic/37638).

ICO (2001) meets the age rule; the port fails the "unofficial ports" rule
outright.

## 2. The existing ICO set

- RA game id **1319**, "ICO (PlayStation 2)" (https://retroachievements.org/game/1319).
- Search results for that page give **32 achievements worth 240 points**;
  one achievement page is "Light in the Dark"
  (https://retroachievements.org/achievement/382746). Both pages answer
  HTTP 403 to `curl` and WebFetch (Cloudflare), so these counts come from
  search-engine snippets and were not read from the page.
- Hash: rcheevos hashes a PS2 disc as MD5 of the boot file name from
  `SYSTEM.CNF`'s `BOOT2` line (prefix `cdrom0:` and backslashes stripped,
  cut at `;`) followed by the whole boot executable
  (`rc_hash_ps2`, `src/hash/rc_hash_disc.c:983-1021` in
  https://github.com/RetroAchievements/rcheevos at commit
  `683197d8dc92555a51e8280a4123d071ed66bff3`). For SCES-50760 that is
  MD5(`SCES_507.60` + `baseelf.elf`) = `7381406270613da02d3940a8f6eed672`
  (computed locally from `baserom/pal/baseelf.elf`). The Connect API
  resolves it without login: `POST https://retroachievements.org/dorequest.php`
  with `r=gameid&m=7381406270613da02d3940a8f6eed672` returned
  `{"Success":true,"GameID":1319}`. So the PAL retail ELF this port
  targets is a recognised hash of the existing set.
- Achievement logic (the `MemAddr` strings, hence the addresses the set
  reads): not obtained. `r=patch&g=1319` and `r=achievementsets&g=1319`
  return 401 "Invalid user/token combination"; the web API
  (`API_GetGameExtended.php`) needs an API key; the game page and code
  notes are behind the 403 above. The address mapping below is therefore
  a method, not a result. With a user's own RA login (their token, used
  locally, never committed) the patch data could be read and the mapping
  run.

## 3. What a native rcheevos client would need

From rcheevos (MIT, same commit):

- `rc_client` (`include/rc_client.h`): create a client with a memory read
  callback and a server call callback, log in, load the game by hash,
  call `rc_client_do_frame` once per emulated frame, and `rc_client_idle`
  otherwise. The port would compute the same hash from the user's ISO at
  first run (the extractor already reads `SYSTEM.CNF` and the ELF).
- Memory map the set's addresses are written against:
  `_rc_memory_regions_playstation2` in `src/runtime/rc_consoleinfo.c:812-819`:
  RA address `0x00000000-0x000FFFFF` kernel RAM, `0x00100000-0x01FFFFFF`
  system RAM (real address = RA address), `0x02000000-0x02003FFF`
  scratchpad (real address `0x70000000`). The read callback gets an RA
  address and a byte count and must return the bytes the EE would hold
  there.
- Hardcore: the client must disable or refuse cheats, save states,
  slow-down and anything that reduces challenge, per RA's rules. The
  port's Developer mode (plan, Phase 6) would have to force Casual mode, as
  it already suspends the port's own achievements.
- Console id: an approved standalone would be `RC_CONSOLE_STANDALONE` (102,
  `include/rc_consoles.h:101`) with its own set; reusing set 1319 means
  claiming to be PS2 hardware (`RC_CONSOLE_PLAYSTATION_2` = 21), which the
  policy forbids for a port.

## 4. A retail-address memory view

What the read callback would have to provide, by kind of address
(inference from the plan's architecture and the tree; nothing below has
been run):

| what the set reads | where it lives in the port | can the view serve it? |
|---|---|---|
| a global in `.data`/`.sdata`/`.bss` (e.g. `gFlagGameClear` at `0x0063AA00`, `gFlagSaveStage` at `0x0063AA04`, both in the layout link and `config/symbol_addrs.pal.data.txt`) | a host global of the same name | yes, by a table from retail address range to host symbol and size. The committed lists name 6,338 globals (`config/symbol_addrs.pal.txt` with `.pal.data.txt`), but 1,058 are no longer globals of the link and 3 sit elsewhere (tools/README.md, `--symbol-map` section); file statics such as `gflags[50]` (`ico2/script/src/gflag.c`, `static unsigned char gflags[50]`, the story flags an achievement set is most likely to read) are not in the lists at all and would need a generated map from the layout link |
| a field of a global struct | host global plus offset | only while the struct is laid out as on the EE: true in the 32-bit `ref` build; in the 64-bit build only for structs frozen as disc/save layout (loader-census.md, 2B) |
| a heap object reached by a pointer chain (RA `AddAddress` reading a pointer, then an offset) | an `iosMalloc` arena block | the pointer value read from memory is a host address. The view would have to translate every pointer word it returns back to a retail-style address, which needs the heap arena to place blocks at the EE offsets the game would and every pointed-to struct to keep EE layout. Not true of the 64-bit build |
| the GS/VU/IOP side (sound state, frame counters in kernel RAM) | not modelled | no |

Feasibility: a read-only view over named globals is a small table and is
feasible; a faithful view of the game's heap is feasible only in the 32-bit
build with an EE-shaped arena, which is never shipped. Since RA would not
accept the port anyway, building this now has no user.

## 5. Precedent: Dusklight

Dusklight (https://github.com/TwilitRealm/dusklight, CC0-1.0 per GitHub;
a Twilight Princess PC port from that game's decompilation):

- Issue #770, "RetroAchievements Support?", closed as not planned
  (2026-05-10). A contributor replied that standalone support needs admin
  approval, an RA developer in good standing on the team and Hardcore
  enforcement.
- Issue #1108, "[Feature Request] RetroAchievements Integration", closed as
  a duplicate. A maintainer wrote that RA's policies make it impossible
  for them, and another wrote: "We reached out to RA and they declined."
- What they built instead (read from `src/dusk/achievements.h`, summarised,
  nothing copied): a port-owned achievement system. Achievements are a
  static list with a key, name, description, category (including a
  "Glitched" category excluded from totals), an optional counter goal and
  progress. Each has a check function run on every game tick. Game code
  raises named signals at event sites (enemy killed, arrow hit at range),
  visible to all checks for that tick then cleared. Per-achievement extra
  state is saved as JSON alongside unlocks. Unlock toasts go through the
  UI overlay with a setting to disable them, and achievements are only
  reachable in game modes that use them.

This matches the plan's Phase 6 design (`port/include/ico_gamestate.h`,
built-in set, popup overlay). Two ideas worth taking as design, not code:
event signals raised at a handful of game sites, with checks reading a
game-state interface rather than raw memory; and a "glitched" category so
sequence-break achievements do not count towards completion.

## 6. Recommendation

1. Do not integrate rcheevos or target set 1319. The policy excludes
   unofficial ports (line 22, since 2026-06-29), Dusklight asked and was
   declined, and reusing the PS2 set would misrepresent the port as
   hardware.
2. Build the port-owned system as planned, with an `ico_gamestate.h`
   interface of named, typed queries (stage number, story flags through
   `gflagChk`, game-clear state, play time) and event signals, never raw
   addresses, so it works identically on the 32- and 64-bit builds.
3. Keep rcheevos as a possible later backend: if RA's policy changes, an
   `rc_client` integration would sit behind the same interface, with a
   new standalone set written against it, not against retail addresses.
4. If a user wants their RA PS2 progress, the answer is the PS2 version in
   an RA-supported emulator.

## Open questions

1. The exact addresses set 1319 reads (needs an RA login; can be done
   locally by the user without committing anything).
2. Whether RA would consider a decomp port if the team included an RA
   developer; the policy text says no regardless.
