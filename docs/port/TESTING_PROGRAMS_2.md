# Test programs, continued

Cases added to programs whose entries are in [TESTING_PROGRAMS.md](TESTING_PROGRAMS.md) (the repo
keeps every file under 256 KiB), and programs whose entries that page has no room for, by the
directory the program lives in. The entries read as that
page's do ([Reading the entries](TESTING_PROGRAMS.md#reading-the-entries)).

## tools

- `check_drop_ledger.py` (`drop_ledger_gate`, `drop_ledger_gate_selftest`; `rd_ledger.cmake`,
  [SCENE_LEDGER.md](SCENE_LEDGER.md#the-gate)): every place that leaves a model's draw out names a
  ledger reason. The reasons of `RdLedgerReason` and `rd_ledger.c`'s words agree in number and
  each reason has a hook call outside `rd_ledger.c`; every early return of `rd_create_vu_mesh` and
  `rd_draw_vu_mesh` and `rd_tex.c`'s `store` follows an `rd__ledger_` call in its block;
  `rd_note_cull_drop`, `visWalk`, `rd__vis_frame`, `rdtex_find`, `rdtex_replace` and
  `pac_hostBuild` call their hooks; the counted drop sites (the game's object, node and part box
  tests each followed by `REG_CULL_DROP`, the packet box test's remapped zeros and its callers,
  `regHostMesh`'s return without a mesh, the stream's batch cap and the packet notes, the stage
  animations' display flag tests each followed by `STAGE_HOST_HIDE` and their idle rows, the
  draw filter's callers each followed by a hook, the visibility test's NOP) keep their counts;
  the packet box test cannot return 0; the texture reasons and their words agree. The self-test: the rule finds an unledgered early return in a synthetic
  function and passes a ledgered one.
- `code_hash_a64.py --selftest`, its corpus (`code_hash_a64_selftest`): the twins' corpus
  (`tools/test/code_hash_a64_corpus.txt`): every function's hash under rules 1, 2 and 3 is the
  file's, every pair hashes alike or apart as the file says, and every decoder line's written
  registers under rules 3 and under rules 1 and 2 are the file's, as `code_hash_corpus` checks
  the C twin.
- `state_legacy_table.py check`, its rules (`state_legacy_tables`): a table's code hashes are of
  the current rules, or of rules 2 with a `hash_rules_kept` line saying why; a format 3 records'
  table (`record 3`, `hash_verify_rules 2`) holds `syms.gzip` and `hashes.z` alone.
- `test/state_legacy_gate_test.py` (`state_legacy_gate`, [PLATFORM.md](PLATFORM.md#saving-and-loading)):
  the Android package gate on the game's code (`tools/state_legacy_table.py gate`) over a fake
  release library and table and a new library, each a small ELF with a yielding game function,
  an indirect caller, one that holds no frame and a port function, their symbol files and lld
  style maps naming real game source files. The same code passes; the yielding function changed
  with its file unchanged fails naming it, its file and `code_changes.txt`; it passes with its
  file in the changed list, or recorded in `code_changes.txt` with its new hash (not with
  another); the indirect caller is gated, the leaf is not; the new library holding an erratum
  843419 patch site where the release had the load passes, rules 2 telling the two apart and
  rules 3 not.

## port/config/test

- `config_test.c`, added: the first-run template's `yorda_look = "off"` (`config`).
- `sim_keys_test.c`, added: `gameplay.yorda_look` listed with the default `"off"` (19 keys of one
  default text), its carry harmless; a state saved before the key existed is the same with it off
  and a difference with it on (`sim_keys`).
- `config_test.c`, added: the first-run template's `zoom_type = "hold"` and `camera_reset = "off"`
  (`config`).
- `sim_keys_test.c`, added: `gameplay.camera_reset` listed with the default `"off"` (18 keys of
  one default text); a state saved before the key existed loads with it off and is refused with it
  on (`sim_keys`).
- `sim_keys_test.c`, added: `gameplay.zoom_type` listed with the default `"hold"` (17 keys of one
  default text); a state saved before the key existed loads under Hold and is refused under Toggle
  (`sim_keys`).

## port/game/test

- `yorda_look_test.c`: Look at Yorda's rules (`port/game/yorda_look.h`,
  [GAME_FEATURES.md](GAME_FEATURES.md#look-at-yorda)), CPU only, linking the real `camera_reset.c`:
  Off answers 0 over 2000 pseudo-random ticks (and a toggle left on is gone); Hold follows L3, not
  without control or with her hand; Toggle on a press, held without repeat, off on the next press;
  a toggle ends without control, with her hand taken, on a new stage and with a Camera reset (Hold
  keeps looking; Camera reset off changes nothing), and an L3 held through each is no press; the
  Main tick clock (a pause keeps the toggle, L3 down at the first tick back is held, a load's
  rewind); a state's load ends a toggle; the game's side (`yorda_look_live.c`, compiled in with
  the game stubbed) on every disc body: the head turn and the camera on her or on the script's
  call-camera target in the look block's order, the script's flag (none on the June 2001 demo),
  her other stage in that body's order of reads, nothing while R1 runs that itself, nothing off,
  without control, with the stick locked by a script (a toggle ends), Free movement, the Debug
  camera, her hand, the European body's capture cut, on a pass the boy's objects did not run or
  without him; `yorda_look.state` one FRESH block of four ints whose bytes copied back resume the
  toggle (`yorda_look`).
- `variants_test.c`, its developer-mode load (`variants` and its disc runs): a state saved with
  Developer mode on, loaded with it off (`ico_variants_developer_state_loaded`): achievements off
  as after the Debug menu's use, all seven bound switches back at this run's values (seven
  changed, none the second time), a variant on now giving its own value.
- `options_test.c`, added: `yorda_look`'s default, words, setter and reload; text scans: `boyact.c`
  names none of it (no `yorda_look`, no helper, no `|| look`, the four R1 tests for her other stage
  as shipped), `trace_host.c` calls `ico_yorda_look_tick` once; the simulation key (`options` and
  its six disc runs).
- `camera_reset_test.c`: Camera reset's press and stick mute (`port/game/camera_reset.h`,
  [GAME_FEATURES.md](GAME_FEATURES.md#camera-reset)), CPU only: off passes the stick through tick
  for tick over 2000 pseudo-random ticks (no reset, no cut); a press resets once with the cut
  marked, a held press does not repeat, a new press resets again; a stick held at the press reads 0
  until both its values are 0; no reset while not live, a press held from there is no press, and
  not live ends a mute; a reset ends a Zoom type Toggle latch (R2 still held is no new press) and
  leaves Hold's zoom; a copy of the arm's use (the two angles at rest in the press's tick, easing
  back as the game does when off); `camera_reset.state` one FRESH block whose bytes copied back
  resume the mute (`camera_reset`).
- `options_test.c`, added: `camera_reset`'s default, words, setter and reload; a text scan of
  `camera-ico2.c` (four `ico_cam_reset(` hooks, each right before the arm's own look-around test)
  and the simulation key (`options` and its six disc runs).
- `zoom_toggle_test.c`: Zoom type's latch (`port/game/zoom_toggle.h`,
  [GAME_FEATURES.md](GAME_FEATURES.md#zoom-type)), CPU only: Hold returns the game's R2 test over
  2000 pseudo-random ticks of the bit, a script's zoom, the source's liveness, the source and the
  camera mode; Toggle's press and release (in, and stays in), the second press (out), a level held
  50 ticks (one flip); the clears (a script's zoom, a press held through it doing nothing after,
  the source not live and R2 still held when it is live again (a press), a new source, a new camera
  mode, a stage start, R2 held into a new stage); a copy of the camera arm's step choice
  (`camera-root.c`, whose lines `options_test` finds four times) giving the game's steps under
  Hold over 2000 random ticks and Toggle's steps over a press;
  Toggle to Hold while zoomed (cleared), Hold to Toggle while held (no flip); the June 2001
  look-around's test; `zoom_toggle.state` one FRESH block whose bytes copied back resume the latch
  (`zoom_toggle`).
- `options_test.c`, added: `zoom_type`'s default, words, setter and reload; the text scans of
  `camera-root.c` (four `ico_zoom_held(` tests, no `else if (p->now & 2)` left, the two resets,
  the source numbers) and `camera-ico2.c` (the June 2001 look-around's `ico_zoom_wanted`), and the
  simulation key (`options` and its six disc runs).

- `stage_disp_test.c`: `stage_DispAnimation`'s row skip (`StageAnimation.c` compiled in) on each disc
  its main body serves: the European disc leaves row 66 out, the Japanese, Korean and Chinese discs
  row 62, the North American disc none, so its row 66 (`deja_lightning.bga`, the dream's room) is
  drawn; each walk ran to its end. `stage_PlayBgAnimation`'s next frame for a row of step 2 from
  frame 10 at 60 Hz: 12 on every disc at the game's frame step 2; at a frame step of 1, 11 on the
  Japanese and North American discs (the frame-rate formula their builds use) and 12 on the
  European disc (`stage_disp`).

## port/input/test

- `pad_kind_test.c`: the kind of controller pressed last (`port/input/pad_kind.c`,
  [INPUT.md](INPUT.md#devices)): every value of SDL 3.4.18's gamepad type to its kind (Xbox 360,
  Xbox One, Steam: Xbox; Switch Pro, Joy-Con pair: Nintendo; the rest PlayStation) and name,
  values out of range, the kind a sequence of reported presses leaves (keyboard, mouse and touch
  PlayStation) and the changes counted once each (`pad_kind`).

## port/platform/test

- `code_hash_corpus_test.c` (`code_hash_corpus`): the arm64 code hash's C twin against the
  Python twin's hashes on their shared corpus (`tools/test/code_hash_a64_corpus.txt`, the
  argument): real functions of two Android builds of one game source (the bird's brain thread
  with the linker's erratum 843419 patch site and its stub in one; `GetCloth4D`,
  `gameblock_read`, `debug_Menu` and `reg_DispMultiPri`, whose page registers rules 1 and 2
  lost; two more patch sites) and variants (another stub instruction, a stub store, a stub that
  does not branch back, no stub to read, the site's ADRP off page offset 0xff8 / 0xffc, another
  stack slot, a real write of each page register). Every hash under rules 1, 2 and 3 is the
  file's, each pair hashes alike or apart as the file says (the builds' pairs alike under rules
  3 and apart under rules 2; every variant apart under rules 3), 43 instructions' written
  registers under rules 3 and under rules 1 and 2 are the file's, and the patch sites found are
  one in the rc's bird and none in the release's.
- `state_legacy_test.c`, its later cases (`state_legacy`): a format 2 table kept at rules 2 loads
  when its manifest says why (`hash_rules_kept`); the format 3 records' tables load with their
  code hashes alone (rules 2 and 3, address order, a few apart, no texts), and their symbol file
  cuts every function they hash.
- `state_name_test.c`, its later cases (`state_name`): a legacy table kept at rules 2 is read
  (its record says rules 2) and B loads the state; a table of rules 1 is not read
  (`test_legacy`). A format 3 record of rules 2 whose functions hash apart from B's under those
  rules is refused by them and loads through its build's table of code hashes
  (`IcoXbNow.xlate`: each function's row the record's hash and the current rules'), its return
  address translated; a table of another build (one row not the record's hash: `another
  build's`) or checking by other rules is not used, and a function whose current-rules hash
  differs too is refused with `(its instructions differ)` (`test_xlate`). A word on a thread's
  stack at a page start between two named objects, where a 16-byte anonymous constant follows
  an 8-byte one that B put elsewhere: placed by its 8 bytes from the word on, found once in B,
  counted and kept for the log (old and new place, the saving build's nearest symbol, where the
  save found it, the bytes); those bytes twice in B: refused (`lie here more than once`); the
  neighbour kept beside it: the two-sided window places it, none by forward bytes; eight 0x07
  bytes from the word on, found once: refused (one repeated byte); the word a page word too
  (a function with a live frame makes its page by ADRP) and an ordinary address in the arena:
  refused; a page word alone: its ADRP here translates it; a word inside an object that moved
  whole: translated; two words that block: both counted and named (`test_window_page`).

- `dump_zip_test.c`, its scene ledger case (`dump_zip`): with a ledger text set
  (`ico_dump_zip_set_ledger`, [SCENE_LEDGER.md](SCENE_LEDGER.md#the-report)) a capture's zip that
  takes the log holds `scene-ledger.txt` byte for byte beside it; one without the log holds
  neither; with none set the zip is the frame and the log.
- `report_test.c`, its scene ledger case (`report`): with a ledger text set
  (`ico_report_set_ledger`, [SCENE_LEDGER.md](SCENE_LEDGER.md#the-report)) the report holds
  `scene-ledger.txt` byte for byte; with none set the next report has no such entry.

- `state_fiber_test.c`, its `misses` case (`state_fiber`): another build's load counting the
  program words it cannot translate by their place ([PLATFORM.md](PLATFORM.md#across-versions)
  **The record**, `IcoStateMiss`). A fiber that wrote a deep frame and yielded is saved, and its
  image restored with a translation of this program's image onto itself that leaves a 64-byte
  hole; one word into the hole is planted at a time. With none planted nothing is counted. The
  deepest saved word and the last word below the saved stack pointer less 128 bytes (the split
  `ico_state_stack_dead_bytes` gives, the one the save's soft scan uses) are each counted dead,
  the verdict (`ico_state_miss_refuses`) keeps the load, the loaded line's counts read word for word
  and the fiber resumes to its end. The first word of the red zone, a word 64 bytes below the stack
  pointer, one 8 bytes above it and a callee-saved register of the switch context (x19 on arm64,
  rbx on x86-64; the layout checked through the saved stack pointer next to it) are each counted
  live with their fiber, place and offset, and the verdict refuses with the first one named word
  for word. A restore without a translation (a state of this build) leaves the planted word as it
  is and counts nothing. Under the address sanitizer the deep frame may lie on its fake stack:
  the dead cases are then skipped with a line.
- `state_reloc_test.c`, its `misses` case (`state_reloc`): the same counting over synthetic
  regions. `ico_state_stack_dead_bytes` at each boundary (no stack pointer, one below 128, the live
  part starting at the base, 7 and 8 bytes into a word, rounding down, all of it dead); an arena
  with a translated word, two without one and one outside the image: one moved, two live with the
  first named at its offset, the refusal and the counts word for word; the game's data adding to
  the count without taking the first's place, and alone named at its own offset; a stack region
  whose two deepest words lie below the stack pointer less 128 counted dead, the words at sp - 128
  and sp + 16 live, the first named 128 bytes below the stack pointer; the dead words alone kept
  with the counts word for word; a new place clearing the dead range; a word read from a copy
  counted by its own address (the saved registers, the scheduler's table); a number kept as it
  is (`keep_code_numbers`' identity point) not counted while the word beside it is; no miss record refuses
  nothing; a scan of this build (no translation) moves the word by the delta and counts nothing.
- `last_run_skin_test.c` (ctest `last_run_skin`, a program of its own; CPU only): the skin check's
  counts in words (`port/platform/last_run_skin.c`, issue 217,
  [PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#the-last-run-file)): the pipelines field's
  words and the summary's short form word for word on a phone (`(a tile-based phone GPU: none
  compiled)`) and elsewhere, nothing with the check off or without counts, the largest counts
  within `LAST_RUN_SKIN_SHORT_MAX`, a short buffer cut and terminated, none left untouched.
- `ahead_mark_test.c` (`ahead_mark`): the next picture submitted ahead, its mark on the install
  (`port/platform/ahead_mark.c`, [PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#the-last-run-file)):
  the state table: `on` after a device loss, a GPU hang, the watchdog or a crash (whatever
  Android's reason), or with no end written and Android's crash, native crash or no response:
  blocked for the running version, its line word for word; of another version: cleared; `on` after a
  clean end, no end written with no record or an ordinary close by Android's reason (user
  requested, user stopped, low memory, signalled, its resources, the freezer, an update, other),
  or an end not read: cleared, not held off; `blocked` of the running version kept, of another cleared;
  no mark: nothing and no line; other text: cleared. Then on files in the build folder: the
  process's start with no mark says nothing and the in-effect write makes `on <version>` once a
  process, its line once; a loss with the order in effect blocks the version, kept at the next starts
  of it (a clean one and a close from the recent apps between), cleared and on again at the first
  start of the next version; a close from the recent apps (no end, Android's `REASON_USER_REQUESTED`), no
  record, a low-memory kill, an update and a signal: not blocked; Android's native crash with no
  end of ours: blocked.
- `vu_zero_mark_test.c` (`vu_zero_mark`): the zero-area cull's mark on the install
  (`port/platform/vu_zero_mark.c`, [PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#the-last-run-file)):
  the end kind from last-run.txt's end (our own first, `clean, after: <end>` as that end) and
  Android's exit record (every reason; SIGKILL apart from other signals); the stage table, every
  stage against every end, printed (`trying` blocks all but a clean close, `counting` and `on` only
  a crash, an ANR, a signal other than SIGKILL, a device loss, a GPU hang or the watchdog, `failed` and `blocked`
  always); the marks with versions (a block for the version in the mark, never the next; a mark
  without one the running version's; a text not known taken as `trying`). Then the start's step on
  files in the build folder: closed from the recent apps list while counting: not blocked, the
  mark removed, no block exported; a crash while counting: `blocked <version>`, kept at that
  version's next start, removed at a later version's; no evidence while trying: blocked, while counting:
  not; the watchdog and a cleanly closed device loss while on: blocked; a SIGKILL while on: not;
  `ICO_VU_ZERO_MARK` and `ICO_VU_ZERO_VERSION` exported. CPU only.
- `compact_mark_test.c` (`compact_mark`): the index compaction's mark on the install, for its
  Mali-G52 form (`port/platform/compact_mark.c`, the cull's stages and table with its own file and
  words): every stage at a crash blocked for the version in the mark and tried again by the next
  version, a lost graphics device while trying blocked, the lines word for word; a clean close
  while counting not blocked; no mark nothing; a text not known taken as `trying`; an end while
  another attempt's mark said trying (counting or on tried again, trying blocked). Then the start's
  step on files in a folder of its own: no mark, no line, the environment exported; closed from the
  recent apps list while counting: not blocked, removed; a lost device while trying: `blocked
  <version>`, `ICO_COMPACT_BLOCK` word for word, kept, removed at a later version's start; a crash
  while on with the twins' mark (as the previous run left it) trying: not blocked, the twins named,
  the cull's step after it leaving its file alone; the same with the one-pass blend's mark and the
  rate card's file; a leanvs-mark.txt not read; the crash with nothing else trying: blocked; a lost
  device while its own test was trying and the four others counting or on: the five steps in
  diag_host.c's order, it blocked and none of them, each of their lines naming it. CPU only.
- `fastps_mark_test.c` (`fastps_mark`): the fast pixel build's mark on the install
  (`port/platform/fastps_mark.c`, the cull's stages and table with its own file and words): every
  stage at a crash blocked for the version in the mark and tried again by the next version, the
  lines word for word; a clean close while counting not blocked; no mark nothing; a text not known
  taken as `trying`; an end while another attempt's mark said trying (counting tried again,
  trying blocked). Then the start's step on files in a folder of its own: no mark, no line; closed
  from the recent apps list while counting: not blocked; a crash while on: `blocked <version>`,
  kept, removed at a later version's start; no evidence while trying: blocked;
  `ICO_FASTPS_MARK`, `ICO_FASTPS_VERSION`, `ICO_FASTPS_BLOCK` exported; then the three steps in the
  start's order after a crash: the build trying and the cull and the twins counting blocks the
  build only (their lines name it), the twins trying and the build on the twins only. CPU only.
- `onepass_mark_test.c` (`onepass_mark`): the one-pass blend's mark on the install
  (`port/platform/onepass_mark.c`, the cull's stages and table with its own file and words): every
  stage at a crash (blocked for that version, the next one tries again), a close from the recent
  apps while counting and a device loss while counting, an end while another attempt's mark said
  trying (theirs), and the start's step on files after the other three marks' (a crash while it was
  trying: it blocked, the cull, the twins and the fast build not; a crash while the fast build was
  trying: the build blocked, it not).
- `nodiscard_mark_test.c` (`nodiscard_mark`): the discard-free twins' mark on the install
  (`port/platform/nodiscard_mark.c`, the cull's stages and table with its own file and words):
  every stage at a crash blocked for the version in the mark and tried again by the next version,
  the lines word for word; a clean close while counting not blocked; no mark nothing; a text not
  known taken as `trying`; a mark without a version the running one's; each mark blocking only its
  own subject (`vu_zero_mark_blocks_beside` over every stage and end: counting or on not blocked
  while the other was trying, the rest as the table; `nodiscard_mark_decide_beside`'s line). Then
  the start's step on
  files in a folder of its own: no mark, no line; closed from the recent apps list while counting:
  not blocked, the mark removed; a crash while on: `blocked <version>`, kept at that version's next
  start, removed at a later version's; no evidence while trying: blocked, a clean close: not; a
  SIGKILL while on: not, another signal: blocked; `ICO_NODISCARD_MARK`, `ICO_NODISCARD_VERSION` and
  `ICO_NODISCARD_BLOCK` exported, the cull's `vu-zero-mark.txt` and environment untouched; then
  the cull's step and the twins' in the start's order after a crash: the twins trying and the cull
  counting blocks the twins only, the cull trying and the twins on the cull only, both counting
  both. CPU only.
- `last_run_test.c`, its skin case (`last_run`): `last_run_summary_text_with` with the skin check's
  figure between the resets and the free memory word for word, none or `""` as before, issue
  217's reporter's figures with the phase cut and the stage and tick whole, a long phase cut with
  the figures kept; then the run's file with a fake source (`last_run_set_skin_source`): the
  pipelines field holding the words before the note, the summary made again by the beat that sees
  the figure change and only then (the hook's calls counted), the report's words
  (`last_run_skin_now`), and with the check off or no source nothing in the field or the words.
- `perf_secs_test.c` (ctest `perf_secs`; CPU only): pictures a second by stage
  (`port/platform/perf_secs.c`, issue 179,
  [PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#pictures-a-second)): a 10 s block at 25 a
  second with a 0.4 s hitch (its second 15), a stage change inside a second (the second keeps the
  stage of its first step) and the next block's start in the stage then, the part second at the
  end, pictures before any step (`?`), a quiet end, a block under 1 s, seconds past
  the 64 held counted apart, no second begun (no line) and a short buffer; the lines word for
  word.
- `perf_secs_test.c`, its seconds' GPU times and present intervals (`perf_secs`): each second's
  timed pictures' medians (GPU span, VU triangles, reduction, present pass) and longest present
  interval, the block's intervals (p50, p90, the longest; over 2, 3 and 4 ticks of the game's pace
  rounded to whole ticks, 2.5 ticks counted as 3; no tick clause without a pace), the first interval
  of a block from the block before's last present, a block with no present, records and intervals
  past the 2048 held counted, a pass of 0 ms left out of its median, the pictures line alone as
  before; the lines word for word.
- `stall_coincide_test.c`, its table (`stall_coincide`): the block's stalls against the pieces'
  shares (the largest share first, a piece with no name, the stalls naming none), a stall's row
  waiting for its two timed replays after, a row still waiting carried into the next block and
  written there with what it has, a block with only a carried row writing no share line, 13 rows in
  parts of 12 and 1, rows past 256 counted, the 2 s triangles' most (the span's edge, the ring's
  last 256), a pass stall's row naming its pass, a replay whose present and pass both stall counted
  once in the share line (and its piece once, the second stall's when the first named none), the
  count starting again in the next block; the lines word for word.
- `power_read_test.c` (ctest `power_read`, a program of its own; CPU only): the battery for the log
  (`port/platform/power_read.h`, issue 179) from strings in the test: the sysfs numbers (a sign,
  spaces, none, too long), the status and type words, a sample from the sysfs texts (uA and uV as mA
  and mV, a level over 100 refused, every file missing), a sample from the Java side's four ints
  (`INT_MIN` for no current, the status numbers), a block's currents (past 12 counted), voltages,
  level and status, the once and block lines word for word, a short buffer cut; the source picked
  (`power_pick`: the one giving a current first) and a reading's freshness (`power_fresh`, 2.5 s).
- `vmstat_read_test.c` (ctest `vmstat_read`, a program of its own; CPU only): the kernel's memory
  events a block (`port/platform/vmstat_read.h`, issue 179) from strings in the test: a newer
  kernel's text (allocstall by zone and the two refault counts summed, a count of a near name not
  taken), an older kernel's (one allocstall, one refault, no compaction or swap counts, no last
  newline), a text with none of the counts, the block's line (growth, not in the file, a count gone
  back) word for word, a short buffer cut.
- `sys_cell_test.c`, its new-copy case (`sys_cell`): `sys_cell_get_new` gives nothing before a
  write, the copy once, nothing again until the next write.
- `rd_vu_zero_test.c`, its longest clause (`rd_vu_zero`): the 10 s clause with every refusal
  reason, the shared lists and a checked picture at seven-digit counts fits `RD_VU_ZERO_TEXT_MAX`
  whole (the window's buffer).
- `thermal_read_test.c` (ctest `thermal_read`, a program of its own; CPU only, not on Windows):
  the phone's heat and clocks (`port/platform/thermal_read.h`, issue 179) over fixture trees it
  writes in the build folder: the parsers (millidegrees and degrees, kHz, cpu lists, garbage), the
  zones chosen by type (a type of none of these, a garbage temperature, an unreadable one when not
  root, two hundred zones: four chosen, the scan capped at 128), no thermal folder, the CPU
  clusters from cpufreq's policies and from each core's folder, a second's reading, the once, token
  and block lines word for word.
- `rd_perf_where_test.c` (ctest `rd_perf_where`, a program of its own; CPU only): where the time
  went (`port/render/rd_perf_where.h`, issue 179) from a made-up block of the boat scene's shape:
  the seven lines word for word, a piece of 0 ms left out, a list not timed pass by pass, an effect
  without a name, the window's texts absent, no timed picture, a short buffer; the rate card's
  line from the reporter's card of issue 179 (118.29 ns a vertex, 541.76 ns a triangle, 0.003 ms
  a pass): each term `about` on its own, never `at most` or `at least` (list 0: 91.1k triangles
  about 49.4 ms; 273.3k corners about 32.3 ms, inside the triangles', not added; one pass; the
  card's units account for about 49.4 of 49.0 ms, the triangles and the passes; list 3: the
  triangles alone, about 6.6 of 15.0 ms), and `no rate card figure` (no `account for`) for no
  card, none given, another graphics API, an item not measured, one at or below 0, another unit,
  the other pre-pass state (both terms), an empty pass of another size, a list not timed pass by
  pass; with the pre-pass off on both the figures without the pre-pass vertex's words and the
  triangles alone accounted for; no VU or shadow
  volume triangles.
- `perf_secs_test.c`, its phone case (`perf_secs`): the second's last reading, `-` a second with
  none, the block's thermal line, nothing with no reading, the next block from nothing.
- `stall_coincide_test.c`, its phone case (`stall_coincide`): the reading at the row's end.
- `attempt_ledger_test.c`, its where case (`attempt_ledger`): the rows on with their counts, the
  judge's row in any reported state, a row not on left out, nothing taken from the block line.
- `report_test.c`, its marks case (`report`): issue 179's mark files (`vu-zero-mark.txt`,
  `ahead-mark.txt`, `nodiscard-mark.txt`) go in byte for byte when they are there; one not there,
  and all three once removed, have no entry.
- `share_report_test.c`, its marks case (`share_report`): the three mark files last, each when it
  is there; every kind, four zips and the marks fill `ICO_SHARE_REPORT_MAX`.
- `hotkeys_test.c`, its stage case (`hotkeys`): a stage change in the block leaves the stats
  period at 10 s (the block is not cut), before and after the window's own seconds (a stage, then
  another) give their line, the line word for word.
- `attempt_ledger_test.c`, its stalls clause (`attempt_ledger`): the clause names the stall lines'
  rule (`a present over 100 ms or a pass over 8 times its block median and 40 ms`), the Adreno
  row's second count left out where the row is not applicable.
- `stall_coincide_test.c`, its small medians (`stall_coincide`): a piece whose median is under
  0.1 ms left out of the judgement (0.1 kept, 0.099 left out), the VU triangles' ratio only
  against a median of 1 or more.
- `perf_hint_test.c`, its refused line (`perf_hint`): a session refused for three threads on a
  system that gives an update rate: one create call, off, never asked again (no session of other
  threads asked for), the line with the rate word for word; no rate, one thread, and refused after
  a session was made: one call each, the line's words.
- `pace_policy_test.c` and `window_scene_test.c`, the presents line's words (`pace_policy`,
  `window_scene`): the GPU span named as the median of the last 8 timed replays; the GPU gap
  line's stalls named as presents blocked over 50 ms; the hint on the block's mean GPU work
  whatever the median passed, no step named under R^2 0.80 (`fit: R^2 0.75, under 0.80`), the
  fit's R^2 or `the work flat` in its words; every variant of the hint swept with its figures at
  their widest (the GPU's time at and past its 9999.9 ms clamp, the game's rate up to its 999.9
  clamp, the widest scales, each kind of fit): the longest under the window's 512 bytes, a time
  past the clamp printed as the clamp, a rate past it no hint.
- `attempt_ledger_test.c`, its skin-phone case (`attempt_ledger`): the row `the skin check's own
  pipelines left out on a phone` (`ico_attempt_decide_skin_phone`) not applicable with the check
  off and off a phone, each with its words, on with its start words, its two counts in the block
  line word for word and taken per block; in every device class on the Adreno 740 classes (on
  Android), not applicable elsewhere.

## port/rhi/test

- `rhi_vk_test.c`, its `waitSubmittedCell` (`rhi_vk`; the `_vk11` and `_vk10` runs take its
  fence-ring branch): `rhi_wait_submitted`, the submit thread's wait before its present
  ([RHI.md](RHI.md#the-interface) "Frame slots and the submit thread",
  `port/render/rd_submit_thread.h` "the GPU's end first"). A finished list: true at once, no wait
  left in `rhi_gpu_progress`. A list held on the host (`vkr_test_hold_next_submit`) waited for on
  another thread: while it blocks, `rhi_gpu_progress` names that submission, unfinished, with a
  wait time that grows, also after this thread's own fence wait for a finished slot came and went
  beside it (`rhi_wait_frame`: the progress pair of its own, so the GPU hang limit sees one
  submission's wait, `port/platform/gpu_hang.h`); it returns true only after the release 150 ms
  in, the submission then finished and no wait shown. The counter read the samples' lines log
  (`rhi_submitted_finished`, no wait): 0 for the held submission (asked by its value and with 0,
  which takes it as the newest, `rhi_last_submitted`'s), 1 after the release, -1 on the fence
  ring. The samples' wait (`rhi_wait_value_ns`): 0 at once for a finished submission and for 0;
  a held one waited for on another thread, seen by `rhi_gpu_progress`, returns 0 only after the
  release with the time it blocked, and while it blocks this thread submits the next list on the
  other slot (the ahead order's third thread, `port/render/rd_submit_thread.h` "ahead"): the
  submit lands with the next number, the wait and the watchdog's view stay on the held one, and
  the list finishes after the release with no validation error; a result forced to VK_NOT_READY comes back as 1 (the
  sample's unknown, `rd_submit_queue_test`), the device not lost, the next wait 0; on the fence
  ring `RHI_WAIT_NONE` with no wait. A wait whose result is forced to a device
  loss (`vkr_test_force_wait_result`) returns false with the device lost, and the next makes
  none. On the fence ring (the slots one at a time) it returns false at once.
- `rhi_vk_swapchain_test.c`, its `besidePresent` (`rhi_vk_swapchain`; skipped on the fence ring
  of `_vk11` and `_vk10`): a worker thread stands for the submit thread with the slot guard and
  the call guard set as the window's are. Slot A's picture is submitted with its GPU work held
  (released by a third thread 150 ms later) and the worker makes `rhi_wait_submitted` and then
  the present. Beside: this thread recycles slot B and acquires its image before the worker's
  wait ends (another image than A's: the swapchain's image to spare; the line prints the image
  count), and after the worker presented A's image submits and presents B's: no image left
  acquired, the last present shown. Rebuild: `rhi_resize_swapchain` from this thread while the
  worker waits returns only after the worker's whole job (its wait and its present) and makes
  one swapchain at the new size; the next frame draws on it. No validation error.
- `rhi_device_table_equiv_test.c` (`rhi_device_table_equiv`): `PRESENT_FREE` on every Vulkan
  identity on Android and on no other (no rule before the table: the row's own definition).

## port/ui/test

- `ui_test.c`, its popups block (`ui`, `ui_jpn`, `ui_kor`, `ui_cn`): the shortened popup
  messages (the save states' checkpoint and other-version messages, the refused check, the
  restore and backup answers, the Steam failure's body) in every language, each line within the
  panel at the body's size, and the Graphics driver's unreadable answer, a title, at the title's.
- `settings_test.c`, `settings_extra_test.c`, `settings_discs_test.c`, added: Controls' seventeen
  rows with Zoom type and Camera reset between Hold type and Stick fix, shown from the title and the pause menu;
  `testZoomType` steps it from both (Hold, Right to Toggle with the key set and its note on the
  cursor, leaving the page saves `zoom_type = "toggle"`, Left back to Hold); every disc shows it on
  the title reading Hold; `testCameraReset` steps Camera reset the same way (Off, Right to On with
  `camera_reset = "on"` saved, Left back to Off), every disc shows it on the title reading Off
  (`settings`, `settings_extra`, `settings_discs` and their disc runs).
- `settings_test.c`, `settings_extra_test.c`, `settings_discs_test.c`, added: Controls' eighteen
  rows with Look at Yorda between Camera reset and Stick fix, shown from the title and the pause
  menu; `testYordaLook` steps it (Off, Right to Hold and Toggle with the key set and its note on the
  cursor, leaving the page saves `yorda_look = "toggle"`, Left back to Hold and Off, round from
  Toggle to Off); every disc shows it on the title reading Off (`settings`, `settings_extra`,
  `settings_discs` and their disc runs).
- `settings_states_test.c`, its reapply case (`settings_states` and its disc runs): a state saved
  in developer mode loaded with it off also marks the run (achievements off) and puts the bound
  Debug-menu switches back; one saved without it marks nothing.
- `pad_glyphs_test.c`: the button pictures' core (`port/ui/pad_glyphs.c`,
  [UI.md](UI.md#button-pictures)), CPU only: the 24 embedded files read, each set complete, each
  piece's aspect, fill colour and black stroke present at 96 rows, the alpha never past 0x80; the
  Xbox B at 24 and 96 rows (the fill in its stem, the stroke left of it, the four corners clear,
  the upper hole's middle clear at 96 rows) and A (the fill in its left leg, the stroke's black in
  its hole, no blue); two squares turning the same way, the middle empty with `evenodd`, filled
  without; the 40 x 24 LB and the 24 x 32 b at their sizes with fill in their letters; LT, ZL and a
  curved shape in absolute forms drawing as the relative ones; a file reading as `xbox/cross.svg`
  drawing as it, byte for byte; eleven broken files (an element that draws, a transform, an arc,
  an open subpath, no viewBox, two paths, a style property, a fill attribute, no closing tag, over
  8192 bytes, over 512 commands) refused through `pad_glyphs_test_source` with a reason, their
  set incomplete and refused as the style in force, a set broken while in force falling back to
  PlayStation, the files back after (`pad_glyphs`).
