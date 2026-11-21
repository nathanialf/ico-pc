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

## port/game/test

- `stage_disp_test.c`: `stage_DispAnimation`'s row skip (`StageAnimation.c` compiled in) on each disc
  its main body serves: the European disc leaves row 66 out, the Japanese, Korean and Chinese discs
  row 62, the North American disc none, so its row 66 (`deja_lightning.bga`, the dream's room) is
  drawn; each walk ran to its end (`stage_disp`).

## port/input/test

- `pad_kind_test.c`: the kind of controller pressed last (`port/input/pad_kind.c`,
  [INPUT.md](INPUT.md#devices)): every value of SDL 3.4.18's gamepad type to its kind (Xbox 360,
  Xbox One, Steam: Xbox; Switch Pro, Joy-Con pair: Nintendo; the rest PlayStation) and name,
  values out of range, the kind a sequence of reported presses leaves (keyboard, mouse and touch
  PlayStation) and the changes counted once each (`pad_kind`).

## port/platform/test

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
- `vu_zero_mark_test.c` (`vu_zero_mark`): the zero-area cull's mark on the install
  (`port/platform/vu_zero_mark.c`, [PLATFORM_DIAGNOSTICS.md](PLATFORM_DIAGNOSTICS.md#the-last-run-file)):
  the end kind from last-run.txt's end (our own first, `clean, after: <end>` as that end) and
  Android's exit record (every reason; SIGKILL apart from other signals); the stage table, every
  stage against every end, printed (`trying` blocks all but a clean close, `counting` and `on` only
  a crash, an ANR, a signal, a device loss, a GPU hang or the watchdog, `failed` and `blocked`
  always); the marks with versions (a block for the version in the mark, never the next; a mark
  without one the running version's; a text not known taken as `trying`). Then the start's step on
  files in the build folder: closed from the recent apps list while counting: not blocked, the
  mark removed, no block exported; a crash while counting: `blocked <version>`, kept at that
  version's next start, removed at a later version's; no evidence while trying: blocked, while counting:
  not; the watchdog and a cleanly closed device loss while on: blocked; a SIGKILL while on: not;
  `ICO_VU_ZERO_MARK` and `ICO_VU_ZERO_VERSION` exported. CPU only.
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
  stage of its first step) and the block's cut asked from then until the line is taken, the part
  second at the end, pictures before any step (`?`), a quiet end, a block under 1 s, seconds past
  the 64 held counted apart, no second begun (no line) and a short buffer; the lines word for
  word.
- `hotkeys_test.c`, its cut case (`hotkeys`): the stats period at most 2 s with a stage change in
  the block (`ico_stats_period_cut_ns`), F11's 1 s kept; the window's own seconds (a stage, then
  another) asking the cut until their line is taken, the line word for word.
- `attempt_ledger_test.c`, its stalls clause (`attempt_ledger`): the clause names the stall lines'
  rule (`a present over 100 ms or a pass over 8 times its block median and 40 ms`), the Adreno
  row's second count left out where the row is not applicable.
- `stall_coincide_test.c`, its small medians (`stall_coincide`): a piece whose median is under
  0.1 ms left out of the judgement (0.1 kept, 0.099 left out), the VU triangles' ratio only
  against a median of 1 or more.
- `perf_hint_test.c`, its retry (`perf_hint`): a first session refused for three threads on a
  system that gives an update rate asked once more for the game thread alone; made: the game
  thread's alone after a thread change (no setThreads) and the background, the start line's words;
  refused again: off, both calls in the line word for word, never asked again; no rate or one
  thread: one call, the line says why; refused after a session was made: no retry, said so.
- `pace_policy_test.c` and `window_scene_test.c`, the presents line's words (`pace_policy`,
  `window_scene`): the GPU span named as the median of the last 8 timed replays; the GPU gap
  line's stalls named as presents blocked over 50 ms; the hint on the block's mean GPU work
  whatever the median passed, no step named under R^2 0.80 (`fit: R^2 0.75, under 0.80`), the
  fit's R^2 or `the work flat` in its words, the longest hint under the window's 512 bytes.
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
