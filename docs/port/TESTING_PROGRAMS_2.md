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
  counted by its own address (the saved registers, the scheduler's table); no miss record refuses
  nothing; a scan of this build (no translation) moves the word by the delta and counts nothing.
