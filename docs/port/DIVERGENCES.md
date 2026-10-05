# Divergences from the PS2 original

The port's standard (docs/research/float-semantics.md): discrete game state
(integers, flags, RNG state, object and state-machine state, timers) is
identical to the PS2 PAL release, SCES-50760; floating-point results follow
the PS2's documented semantics (EE FPU and VU0) as closely as the port
implements them. Every known case where the port's behaviour or output can
differ from the original, deliberately or not, is listed here, with where it
happens and why. Options that are off by default (widescreen, interpolation,
stick fix, the Yorda option, mirror mode) are not divergences of the default
game and are listed under their own heading only for reference.

## How to add an entry

One row per divergence. Give the site (`file:line` or function), what the
PS2 does, what the port does, whether discrete state can be affected, how it
was found (static audit, unit test, trace diff, user report) and the status
(accepted, to fix, fixed in <commit>).

## Default build

| id | site | PS2 behaviour | port behaviour | affects discrete state | found by | status |
|---|---|---|---|---|---|---|

## Floating point

| id | site | PS2 behaviour | port behaviour | affects discrete state | found by | status |
|---|---|---|---|---|---|---|

## Platform (audio, video, input, saves, timing)

| id | site | PS2 behaviour | port behaviour | affects discrete state | found by | status |
|---|---|---|---|---|---|---|

## Optional features (off by default)

| option | hook | what changes |
|---|---|---|
