# Test programs, continued

Cases added to programs whose entries are in [TESTING_PROGRAMS.md](TESTING_PROGRAMS.md) (the repo
keeps every file under 256 KiB), by the directory the program lives in. The entries read as that
page's do ([Reading the entries](TESTING_PROGRAMS.md#reading-the-entries)).

## port/platform/test

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
- `last_run_test.c`, its skin case (`last_run`): `last_run_summary_text_with` with the skin check's
  figure between the resets and the free memory word for word, none or `""` as before, issue
  217's reporter's figures with the phase cut and the stage and tick whole, a long phase cut with
  the figures kept; then the run's file with a fake source (`last_run_set_skin_source`): the
  pipelines field holding the words before the note, the summary made again by the beat that sees
  the figure change and only then (the hook's calls counted), the report's words
  (`last_run_skin_now`), and with the check off or no source nothing in the field or the words.
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
  in, the submission then finished and no wait shown. The verdict's counter read
  (`rhi_submitted_finished`): 0 for the held submission (asked by its value and with 0, which
  takes it as the newest), 1 after the release, -1 on the fence ring. A wait whose result is forced to a device
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
