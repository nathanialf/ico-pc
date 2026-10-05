# TODO

Work that is wanted but not started. Each entry says what, why, and where
it would go. Finished work is not listed here; `git log` has it.

## Display

- **CRT filter.** A display option (Settings > Display) that renders the
  presented picture through a CRT shader: scanlines, phosphor mask, a
  little bloom and curvature, tuned so the Original preset at 4:3 looks
  like the PS2 on a period television. Off by default. It is a present-time
  post pass in `port/render/rd_present.c` over the DISPLAY target, with
  its own HLSL in `port/shaders/`; it must not touch the scene passes, so
  frame dumps and the Original pixels stay what they are. Strength and
  mask type as config keys (`[video] crt`, `crt_strength`) with Settings
  rows, documented in DISPLAY.md and CONFIG.md.
