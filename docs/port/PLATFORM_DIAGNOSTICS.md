# The platform's diagnostics

The log, the heartbeat and watchdog, crash and bug reports, the loss ladder and the diagnostics the window prints. The rest of the subject is [PLATFORM.md](PLATFORM.md)'s.

The log is `logs/ico-pc.log`, stdout and stderr redirected there, rewritten each run with the run
before kept as `logs/ico-pc-previous.log`; `--console` keeps them on the console
(`port/platform/main_host.c` (`keep_previous_log`); `port/platform/host_config.h`
(`ico_host_redirect_output`)). After the start time and folder the log's next line is
`ico_pc: version <label> (commit <hash>)`, or `ico_pc: development build (commit <hash>)` when the
build has no package label (`port/platform/main_host.c` (`host_main`); `CMakeLists.txt`
(`ICO_VERSION_LABEL`)). On Windows the streams are fully buffered and the host loop flushes
them once per vsync; elsewhere they are unbuffered. Other files in `logs/`: the
trace (`trace-<time>.txt`, by default in the headless build only), the
pad recording (`input-<time>.txt`, by default in the window build only), `ico-pc-perf.csv` with
`[dev] perf_log`, and `audio.wav` with `audio_dump=1` (`port/platform/main_host.c` (header
comment); `port/platform/window_host.c` (the perf record comment)).

`ico_diag_init`, called as soon as the log is open, installs the crash handlers: a vectored handler
and the unhandled-exception filter on Windows, SIGSEGV, SIGBUS, SIGILL, SIGFPE and SIGTRAP on an
alternate stack on POSIX, and `abort` (asserts, `ico_assert`, the scheduler's fatal errors). A crash
writes the cause, the faulting address as module+offset, the current game thread and its last
kernel call, the Vulkan render pass last begun and the renderer's last 16 pass ring entries
([RHI.md](RHI.md#device-loss), **The crash report's pass**), return-address candidates, the
heartbeat line and every thread, then the
[symbol lines](#the-symbol-lines) naming each offset and the [report file](#the-report-file), then
on Windows shows a message box naming the report and exits with code 3 (`port/platform/diag_host.h`;
`port/platform/CMakeLists.txt` (the link map)). Every
diagnostic line is written with one unbuffered OS call to its own handle on the log, so a crash
leaves everything before it (`port/platform/diag_host.c` (header comment)). On Android the handler
then puts the system's handlers back so the system still writes its tombstone
(`port/platform/diag_host.c` (`restore_handlers`)).

On Windows, three exception codes are messages to a debugger and never crashes: 0x406D1388 (the thread-naming exception, which SDL raises for every thread it makes and some drivers raise too) and the two OutputDebugString codes 0x40010006 and 0x4001000A. Both handlers return `EXCEPTION_CONTINUE_EXECUTION` for them (the raise returns to its caller, as SDL's own temporary handler does) and log `diag: thread-name exception from <module> ignored (thread <id>)` once per raising module, at most eight modules, the module being the first stack frame outside kernelbase.dll and ntdll.dll; the unhandled filter's line ends `(reached the unhandled filter)` (`port/platform/diag_host.c` (`ico_diag_benign_exception`, `benign_note`); issue 142, where the filter reported 0x406D1388 as a crash and the report's wait ended the run a minute later).

A crash while the renderer builds a pipeline is a graphics driver crash: the activity note
(`ico_diag_set_activity`) starts with "creating pipeline" or "creating the start-up pipelines". On
Windows, Linux and macOS the handler then starts the program again at once, with
`ICO_STARTUP_RETRY=<step>:<backend>` in the new run's environment; nothing is written to the user
folder or the settings (an end without a crash report has a step of its own, which writes one
on the second such end in a row: below). The steps come in order, each taken only when the run before crashed:
`cache` (the crashed run had loaded the pipeline cache file: it is deleted and the same backend
tries again), `targets` (a `_store_pre_vs` build:
[PIPELINES.md](PIPELINES.md#the-record-stores-restart-rung)), `noprepass` (the note names a pipeline of the VU vertex pre-pass or of the record
path that draws from it: a ` pre` key, a `_pre_vs` module (the pre-pass's entries, the record
stages' `vu_tri_*_pre_vs` and `vu_particle_pre_vs`), a `_main_vs` module (`vu_main_vs`,
`vu_particle_main_vs`, made only with the pre-pass) or the old triangle stage's `vu_tri_vs` or
`vu_tri_plain_vs`, `startup_retry_note_has_vu_pre`; the same backend without the pre-pass, `ICO_RD_VU_PREPASS=0`
from `ico_ini_export`, which makes none of them, [RENDER.md](PIPELINES.md#the-vertex-and-triangle-pre-passes)),
`fixedblend` (the note names a pipeline whose `dst` field is nonzero, an exact-blend
pass with a fragment shader interlock or rasterizer-ordered view entry: the same backend with the
fixed-function blend, `ICO_RD_BLEND=fixed` from `ico_ini_export` as for the loss steps, which builds
no such pipeline; skipped when the player chose the blending with `blend=` or `ICO_RD_BLEND`, so
with the fixed-function blend the default, which makes no destination-read pipeline, it is reached
only by a run whose exact blend the player did not choose;
`startup_retry_note_has_dst` reads the note), `d3d12` (a build with the Direct3D 12 backend whose
player named no backend; its own blending, not the fixed one), `lazy`
(each pipeline is built when a frame first needs it), then none: the crash ends as any other, with
the driver-crash text in the crash box where the platform shows one (`port/platform/startup_retry.h`
(`startup_retry_next`); `port/platform/diag_host.c` (`driver_crash_plan`, `driver_box_text`)).
The runs of `noprepass` and of every step after it draw without the pre-pass
(`startup_retry_draws_no_prepass`; the window also tells the renderer, `rd_set_vu_prepass_off`,
so its line says `rd: VU pre-pass: off for this restart (the <step> step after a graphics driver
crash)`), so a later step never makes its pipelines again; the pictures are the same.
Everything the relaunch needs is captured at start-up, so the handler does not allocate
(`port/platform/diag_host.h` (`ico_diag_prepare_relaunch`)). On Windows the relaunch is
`CreateProcessW` with the module path, `GetCommandLineW` and a copy of the environment block, and
the crashed process then ends; on Linux and macOS it is `execve` of `/proc/self/exe` or
`_NSGetExecutablePath` with the same `argv`, which keeps the process id (`port/platform/diag_host.c`
(`relaunch_now`); `port/platform/startup_retry.h` (`startup_retry_block_copy`,
`startup_retry_envp`)). Android and iOS do not relaunch. The new run appends to the crashed run's
log instead of keeping it as `ico-pc-previous.log`, logs
`ico_pc: restarted after a graphics driver crash while preparing the shaders: <what this run does>`,
and takes the step's backend
unless the player chose one (`port/platform/main_host.c` (`host_main`);
`port/platform/window_host.c` (`ico_window_open`); `port/platform/startup_retry.c`
(`startup_retry_what`)). ctest `startup_retry` checks the steps and the relaunch's environment
(`port/platform/test/startup_retry_test.c`).

A lost graphics device (the driver reset the card, `DXGI_ERROR_DEVICE_HUNG`, `VK_ERROR_DEVICE_LOST`:
`rhi_device_lost`, not a crash) restarts the program too, on Windows, Linux and macOS. The window
logs the loss and the renderer's pass ring, then asks for the loss step
(`port/platform/window_host.c` (`device_lost_quit`); `port/platform/diag_host.c`
(`ico_diag_device_lost_restart`)); the steps come in order, each taken only when the run before
lost the device: `losstargets` (a run with the stores), `lossnoprepass` (the same backend without the VU vertex pre-pass,
`ICO_RD_VU_PREPASS=0`, when the losing run drew with it after its self-test:
`ico_diag_set_vu_prepass`, `StartupRetryRun.prepassWasOn`), `robust` (the same backend with robust
buffer and image access, `ICO_GPU_ROBUST=1`, when the losing run drew with it off by the device's
default, `StartupRetryRun.robustOffDefault`: the window sets it once the device is made from
`RhiLimits.robustDefaultOff`, which the Vulkan backend sets where it makes `robustText`'s `off (the
default; ...)` (`ico_diag_set_robust_off_default`), so every Vulkan run without `gpu_robust` takes it
and Direct3D 12, whose flag is false, never does; the
step's run sets `ICO_GPU_ROBUST_AFTER_LOSS=1` before the device is made, as the window has already
dropped `ICO_STARTUP_RETRY`, so its device line says `robust access on, ... (the restart after the
graphics card stopped responding)` (`port/rhi/vk/vk_device.c`)), `blend` (the same backend with the fixed-function blend, `ICO_RD_BLEND=fixed`, so
no fragment shader interlock or rasterizer-ordered view passes; skipped when the losing run already
drew with it, the default, or the player chose the blending with `blend=` or `ICO_RD_BLEND`), `backend` (the
other backend, Direct3D 12 from Vulkan or Vulkan from Direct3D 12, with the fixed-function blend; in
a build with both, when the player named no backend and no blending other than `fixed`), then none
(`port/platform/startup_retry.h` (`startup_retry_next`, `StartupRetryRun`); `port/platform/startup_retry.c`
(`loss_next`)). Every loss step's run draws without the pre-pass, `robust` with robust access, and
`blend` and `backend` with the fixed-function blend. With a step, the session ends through the normal quit path without a box (the
settings are saved and the window, audio and files closed as at any quit) and the program starts
again at exit, after the end-of-run steps, with `ICO_STARTUP_RETRY=blend:<backend>` or
`backend:<backend>`; the relaunch is the same `relaunch_now` as the crash's, run from an `atexit`
handler registered before the end-of-run steps (`port/platform/diag_host.c`
(`loss_relaunch_at_exit`, `loss_relaunch_register`)). When it cannot start the program, Windows and
Linux show a box. Without a step the window shows its box, which says the game already restarted
itself when the run was a loss step's. Android and iOS keep the box; Android's next start takes
the `robust` step's measure itself (below). The new
run starts at the title as a fresh launch (what was not saved was lost with the card, as at a quit),
appends to the log, logs
`ico_pc: restarted after the graphics card stopped responding: <what this run does>`, and gets
`ICO_RD_BLEND=fixed` (the `blend` and `backend` steps), `ICO_GPU_ROBUST=1` (the `robust` step) and `ICO_RD_VU_PREPASS=0` from
`ico_ini_export`, which win over `blend=` and `vu_prepass=` and over the environment for that run
(the step is never taken against a player's blending other than `fixed`)
(`port/platform/host_config.c` (`ico_ini_export`, `ico_dev_env_export_value`)). When the
`backend` step's Direct3D 12 cannot start, that run goes on with Vulkan itself
(`port/platform/window_host.c` (`ico_window_open`)). Apart from the steps, a start whose backend
the player did not name (`[video] backend` empty: Automatic) and whose Vulkan does not start (SDL
refuses the `SDL_WINDOW_VULKAN` window, as without a Vulkan loader, or `rd_init` fails: no device,
a device the renderer cannot use) goes on with the next backend the build links, once each
(`window_fallback_backend`, [BACKENDS.md](BACKENDS.md)): Direct3D 12 where it is linked, then
OpenGL (`rhi_backend_name` lists them): `ico_pc: no <failed> device; using <next>`, the window
destroyed and made again with the flags the backend needs (`window_flags`; for OpenGL after
`window_gl_attributes`: double buffer, no depth, stencil or sRGB, 4.3 core),
`ico_diag_set_startup_retry` told the new backend, then `window: Graphics API <next> (automatic,
after <failed>)`; when the last one does not start either, the start ends as before. A backend
the start already tried is skipped, the restart's Direct3D 12 included (`window_fallback_tried`). A crash or loss on it takes the orders above with `opengl` as the
backend (the `d3d12` and `backend` steps go to Direct3D 12 where the build has it; the steps
themselves are unchanged: there is no OpenGL or Direct3D 11 step). See
[BACKENDS.md](BACKENDS.md). The step only grows (the loss steps come after
`lazy`), so a crash in a loss step's run ends with the crash box and every chain ends. Nothing is
written to the user folder. ctest `startup_retry` checks both orders over every input
and the loss values in the relaunch's environment, and the chains with the pre-pass
(`test_prepass_chain`); ctest `host_config` checks the `ICO_RD_BLEND` and `ICO_RD_VU_PREPASS`
precedence (`port/platform/test/host_config_test.c` (`test_retry_blend`, `test_retry_prepass`)). ctest `diag` runs the
loss restart end to end with the relaunch prepared and stubbed (`ico_diag_set_relaunch_stub`, so no
process is started): the step and its value (`blend:vulkan`, then `backend:d3d12`), the quit path's
`atexit` steps before the relaunch, nothing at exit when no step is left, and the box when the
relaunch fails (`port/platform/test/diag_test.c` (`loss_child`)).

A start of another version than the activity file's (its version line) logs that a Vulkan renderer
keeps the other version's pipeline cache file when both have the same shaders and otherwise drops
it and prepares the graphics from nothing (Direct3D 12 keeps none; the file is named by the shader
set, [RHI.md](RHI.md#vulkan)), with or without an ended run in the note; a file with no note is one only a loss line
kept, as a clean end deletes it (`port/platform/diag_host.c` (`ico_diag_startup_activity_begin`); the drop itself is `vkr_cache_drop_others`,
[RHI.md](RHI.md#vulkan)). An end without a crash report (the system or the driver ends the
process) is caught by the start-up activity file, `logs/startup-activity.txt`: the version's line, then the activity note. While the
start-up pipeline set is created (`ICO_DIAG_ACTIVITY_STARTUP`) each batch's note (its range of the
set and its first and last key, [RENDER.md](PIPELINES.md)) replaces the text (a careful
set's file adds `set: careful` after it), and the file is deleted when the set is done; a lazy run (`ICO_DIAG_ACTIVITY_LAZY`) records
every first-use pipeline and a line between them for the whole run. The file is opened once when
the recording starts and closed when it ends; each note is one write at the file's start, padded
with NULs over a longer text before it, then the file is cut to the note, so it reads as one whole
note even when the process is ended between the two (`port/platform/diag_host.c` (`act_write`,
`act_open`, `act_close`)). The next start of the same version logs
`ico_pc: the previous run ended while: <note>` and makes the pipelines as the chain's step below
says (carefully or as the frames need them; a note that is no end while making them, as the frames
need them while the file says the run was lazy);
another version's file is logged (`ico_pc: the previous run ended while: <note> (version <old>);
trying the start-up set again on <new>`, without the last clause when the note is an end while
making the pipelines, which the chain's step below counts whatever the version); a lazy run's clean
exit leaves its own
line, logged as `ico_pc: the previous run created the pipelines as the frames needed them; this one
does too`, so the lazy start sticks until the version changes (`port/platform/diag_host.h`
(`ico_diag_startup_activity_begin`, `ico_diag_startup_activity_exit`)).

The chain's step for an end without a crash report (issues 93 and 107). Such an end is ambiguous:
the driver may have killed the process, or the player swiped the app away, the system ended it in
the background, or Task Manager did. So the first one changes one start only, and the second in a
row makes it last. A note that names a pipeline being made (`creating pipeline ...`, `creating
pipelines ...` or `creating the start-up pipelines`), from a start of any version that was not a
restart after a crash report and was in the foreground, counts one end in a row of its kind: the
VU vertex pre-pass's (`startup_retry_note_has_vu_pre`, the modules of the `noprepass` step above,
`vu_main_vs` among them: a batch of the largest VU modules is this kind), or else the pipelines'.
An end inside an extended VU batch of a release that had them (` ext 1`, `vu_X_vs`) names none of
those modules and counts as the pipelines'. The counts live beside the activity file in
`logs/startup-activity-ends.txt` (`pipelines <n>`, `vu_pre <n>`, `loss <n>`, `loss_robust
<0|1>`; an older file's `vu_ext <n>` line is dropped), and each counted end logs `ico_pc: starts in
a row that ended while preparing the graphics (the pipelines): 1` (`(the vertex pre-pass)` for the
other) (`port/platform/diag_host.h` (`ico_diag_startup_end_count`); `port/platform/diag_host.c`
(`ends_count_prev`, `loss_count_prev`)). A count file that cannot be written is logged once a run:
`ico_pc: the count of starts that ended while preparing the graphics could not be written to
<path> (pipelines <n>, vertex pre-pass <n>, device losses <n>)` (`ends_write`).

An end inside the skinned draws' bone-block check's start-up probe (issue 163,
[VU_SKIN_CHECK.md](VU_SKIN_CHECK.md); its notes hold `rd vu skin probe`,
`startup_retry_note_has_skin_probe`: `creating pipeline rd vu skin probe (...)` while its
pipelines compile, `the VU skin check's start-up probe (rd vu skin probe): ...` while it draws and
reads back, each written to the disc before the work) is neither kind: it breaks the pipelines'
row, and the next start, whatever the version and a restart after a crash report too, writes
`[dev] vu_skin_check = false` to `config.toml` at once (the check is a diagnostic, not part of the
picture: no second try), sets `ICO_RD_VU_SKIN_CHECK=0` before the device is made and makes its set
as usual: `window: VU skin check: the previous run ended inside its start-up probe (<note>);
[dev] vu_skin_check = false is written to <config>, so this start and every start after it run
without the check (delete that line to try it again)` (`, which could not be written`: `could not
be written to <config>, so this start only runs without the check`). A probe in which the device
was lost or whose picture could not be read back (`rd_vu_skin_probe_failed`) turns the check off
for the run (`rd: VU skin check: off (the start-up probe failed: ...)`) and the window writes the
same setting right after the self-test, before a loss restart or the loss box: `window: VU skin
check: the start-up probe failed on this device (<why>); ...` (`skin_check_persist_off`,
`skin_probe_after`).

The next start's step, by kind (`ico_diag_startup_pipeline_set`, `ico_diag_startup_vu_prepass`,
`ico_diag_startup_end_persist`; `port/platform/window_host.c` (`ico_window_open`)):

| the previous start | the first in a row: this start only, nothing written | the second in a row: written to `config.toml` |
| --- | --- | --- |
| ended inside a pipeline or batch of the pre-pass or the record path | draws without the pre-pass (`rd_set_vu_prepass_off`; its self-test not run), builds the set as the device does | `[dev] vu_prepass = false` (`vu_prepass_persist`) |
| ended inside any other pipeline, its file without `set: careful` (the full set, a lazy run, a file of a release without the line) | makes the set under the careful policy (`ICO_DIAG_PIPESET_CAREFUL_ONCE`, `rd_pipeline_policy_careful`) | `[dev] pipeline_set = "lazy"` (`pipeline_set_persist_lazy`), this start lazy (`ICO_DIAG_PIPESET_LAZY_NOW`) |
| ended inside any other pipeline, its file with `set: careful` (the device table's careful set, `pipeline_set = "careful"`, or the careful start the row above gave) | makes no set: lazy (`ICO_DIAG_PIPESET_LAZY_ONCE`), its notes and clean exit saying `for this start only` | the same |
| lost the graphics device, on Android, `gpu_robust` not set (a `config.toml` `true` without the mark is removed first, `window_robust_start`, so it does not count as set) | draws with robust access on (`ICO_GPU_ROBUST=1`, `ICO_GPU_ROBUST_AFTER_LOSS=1`) | `[dev] gpu_robust = true` (`gpu_robust_persist`), this start robust |

A loss counts as a lost start through the activity file's loss line, written by
`ico_diag_note_device_loss` when the main loop's loss path makes its dump. A loss found at the
lifecycle's wait for the GPU as the app goes to the background (`window_host.c`
`lc_gpu_wait_idle`, `VK_ERROR_DEVICE_LOST` at `vkDeviceWaitIdle`) is noted there at once
(`ico_diag_note_device_loss_at_wait`: `ico_pc: the graphics device was lost at the GPU wait as the
app went to the background; counted now as a start that lost it ...`), because the system may end
the app in the background before that loop runs again (issue 179's reporter: the next start said
`no lost start`); the dump, when the app comes back, replaces the line with its own. The system
ending an app in the background without a loss writes no loss line and counts nothing.

The careful set's start writes `set: careful` after the note in the activity file
(`ico_diag_startup_activity_careful`), which is how the next start tells the two apart, so the
first end of a full set makes the next start careful and the first end of a careful set makes it
lazy; the second end in a row writes `lazy` whatever the set was. The lines: the pre-pass's kind
`ico_pc: the previous start ended while the graphics driver compiled the 3D vertex pre-pass's
shaders (<note>); this start only draws without it (nothing is written to <path>); if a start that
compiles them ends there again, [dev] vu_prepass = false is written for good`, the second `...
this start and every start after it draw without it ([dev] vu_prepass = false in <path>); delete
that line to try it again`; the pipelines' careful step `ico_pc: the previous start ended while
preparing the graphics (<note>); this start only makes the start-up set the careful way (small
batches on one thread, the pipeline cache written after each; nothing is written to <path>); if
this start also ends while preparing them, [dev] pipeline_set = "lazy" is written for good`, the
renderer's `window: pipelines: careful start-up set (the previous start ended while preparing them; for
this start only): batches of 8 on 1 thread, the cache written after each, stopped past 128 MB of
growth`; the lazy step `ico_pc: the previous start ended while preparing the graphics (<note>);
this start only makes the pipelines as the game needs them, with no start-up set (nothing is
written to <path>); if this start also ends while preparing them, [dev] pipeline_set = "lazy" is
written for good`, the renderer's `rd: pipelines: no start-up set (the previous start ended while
preparing them in the careful set; for this start only): ...`; the second `ico_pc: the previous
start ended while preparing the graphics (<note>); this start and every start after it make the
pipelines as the game needs them, with no start-up set ([dev] pipeline_set = "lazy" in <path>);
delete that line to try the start-up set again` and the renderer's `[dev] pipeline_set = "lazy"
from now on`. The lazy run's notes between two creations and its clean exit say `for this start
only`, so the start after a clean exit logs `ico_pc: the previous run made the pipelines as the
frames needed them for that start only; this one tries the start-up set again`, and the start
after one ended between two creations `ico_pc: the previous run ended while: running with the
pipelines created as the frames need them for this start only (none was being created); that lazy
run was for that start only, so this one tries the start-up set again`. All of these lines come
once the device is made, but the loss's (before it). The count stays in the file until the second
writes its setting: a start that is due but fails before the device is made leaves it, and the
next end in a row is due again. When the write fails the line ends `, which could not be written:
this start only`, the renderer's line says `[dev] pipeline_set = "lazy" could not be written, so
for this start only`, the lazy run's notes say `for this start only`, and the count stays, so a
further end is due again. With `[dev] pipeline_set = "lazy"` already set the pipelines' count is
cleared and no line about it follows. It is the crash order's `lazy` step made to last, for the
ends a crash handler never sees (the system ending the app, a device that freezes); a crash with a
report keeps its own order above. The setting outlives the version; deleting the line makes the
next start try the set again ([RENDER.md](PIPELINES.md#the-start-up-set)).

A careful set that its memory budget stopped (`rd_precreate_pipelines` called `rd_pipelines_lazy`,
`rd_pipelines_lazy_on`) is a set that ended as it should: the window calls
`ico_diag_startup_activity_budget_stop`, which clears the pipelines' row as a finished set does and
records the rest of the run as a lazy run for this start only (`ICO_DIAG_ACTIVITY_LAZY`, the
`set: careful` line kept), so a freeze in a later first-use pipeline is named and counted (the
next start lazy for that start) and a clean exit makes the next start try the set again.

On Android the system's low-memory signal stops the start-up set too, under any policy
(`port/platform/diag_host.c` (`low_memory_stop_note`, `ico_diag_low_memory_stop`)):
`ico_diag_note_low_memory` sets an atomic level from the activity's thread before it logs its line,
`onTrimMemory` 15 (`running critical`) at once and 10 (`running low`) only once the set is past its
first batch (`ico_diag_set_pipelines`' count above 0: a `running low` while the app is opened can be
the system making room for it); 5 and a signal without a level never stop it. The set's loop asks
`ico_diag_low_memory_stop` through `rd_set_low_memory_fn` after each batch (the window registers it
before `rd_init` on Android only; desktops never get the signal), so the batch in flight finishes
(the careful policy's batches are 8 pipelines on one thread, so nothing cuts a batch short), the
pipeline cache is written, and the rest of the set is made on first use
([RENDER.md](PIPELINES.md#the-start-up-set)). The window's `ico_diag_startup_activity_budget_stop` then
logs that it stopped (the next start resumes the set) and records the rest of the run as a lazy
run not for this start only, with the line `set: stopped at the system's low-memory signal
(running critical)` after the note (and after `set: careful`), the set's `compiled: ` and `system: `
lines kept, and the window appends where it stopped (issue 123, `ico_diag_startup_set_resume_part`).
The next start of the version that finds the line beside the lazy run's clean exit or its note
between two creations is `ICO_DIAG_PIPESET_LAZY_LOW_MEMORY`, which the window turns into the set
resumed at that place ([PIPELINES.md](PIPELINES.md#stop-and-resume)). A start whose file has the line and that ended inside a
first-use pipeline logs `ico_pc: the previous start had stopped preparing the graphics when the
system ran low on memory (<line>)` and counts as an end while preparing the graphics: the line
counts as `set: careful` does, so the first end in a row makes the next start lazy for that start
(`ICO_DIAG_PIPESET_LAZY_ONCE`) and the second writes `[dev] pipeline_set = "lazy"`. A restart after a
crash report and another version's file take their own steps above.

Android cannot start the game again after a device loss, so the `robust` loss step's measure
comes at the next start instead (`port/platform/window_host.c`, `#ifdef __ANDROID__`): the
activity file's loss line (`loss dump: <zip>` or `graphics device lost, no loss dump`,
`ico_diag_note_device_loss`) counts one more in the row of starts that lost the device, whatever
the version or the note (`ico_pc: starts in a row that lost the graphics device: 1`;
`loss_count_prev`); after the loss bisect and before the renderer starts, the first in a row sets
`ICO_GPU_ROBUST=1` and `ICO_GPU_ROBUST_AFTER_LOSS=1` for this start (`ico_pc: the previous start
lost the graphics device; this start only checks the bounds of every buffer and image read
(robust access; nothing is written to <path>); if a start loses it again before one runs without a
loss, [dev] gpu_robust = true is written for good`) and marks the count file (`loss_robust 1`);
that start's end without a loss leaves the row (the mark is read and cleared), any other start
without a loss ends it. So the second in a row is a loss in the robust start itself or in the
start after it, with robust access off again: it writes `[dev] gpu_robust = true` and logs `ico_pc:
the graphics device was lost in two starts in a row; this start and every start after it check the
bounds of every buffer and image read ([dev] gpu_robust = true in <path>); delete that line to turn
it off again`; the game takes it off again itself (`robust_persist.h`: the mark `[dev] gpu_robust_by`, 3 clean starts in a row or a new version; a clean start is one that reached the game, after the set or after the self-tests of a run on first use, and ended without a loss, an end while preparing the graphics or a watchdog end, the last read from the mark `<activity file>-watchdog.txt` the watchdog writes as it ends a run with the main thread not waiting for the GPU and the next start reads, logs (`ico_pc: the previous run (version V) was ended by the watchdog: <reason>`) and deletes (`ico_diag_startup_prev_watchdog`); each start's `ico_pc: robust access on (set by the game after 2 lost starts on <version>): N clean starts of 3 before it turns off` line). A `config.toml` `gpu_robust = true` without the mark (Android's releases from before the mark wrote it with none; issue 179's reporter carried one) is taken as the game's own from another version and removed at the start by the same removal (the `[dev]` table's two keys and the root's dotted `dev.gpu_robust` / `dev.gpu_robust_by` lines; any other hand-written form that config.toml's parser still reads as true after it is left as it is, nothing written, the start saying the line must be deleted by hand, and a config.toml read that fails part way gives no text, so a cut copy is never written back), before the device and before this row's step, with this start's `ICO_GPU_ROBUST` emptied unless ico-pc.ini's `gpu_robust=` asks for it: robust access is off from that start, and a loss takes the steps above as with the key unset (`robust_persist.h` `robust_start_step`, `ROBUST_OFF_UNMARKED`; `ico_pc: robust access turned off ([dev] gpu_robust = true in <path> had no gpu_robust_by mark: ...)`, once, as the line is gone after it; the attempt ledger's `the old robust-access line taken off on (...)`). With `gpu_robust` set by the player (the variable already in the environment)
nothing is written, the row kept (`ico_pc: the previous start lost the graphics device (<n> in a
row); nothing is written to <path>: robust access is as the settings say ([dev] gpu_robust)`). A
restart after a crash or a loss (desktop) counts only the loss ladder's `loss_chain` (RHI.md). The rest of the row's
rules are the ends': the count file, the failed write (`, which could not be written: this start
only`, robust access for that start only, the count kept).

What breaks a row: an end in the background (below) breaks the row of its kind; any other note in
the file (the run ended between two creations, or in the loss bisect) breaks the pipelines' row; a
start-up set that finished (or stopped at its budget), or a lazy run's normal exit, clears the
pipelines' row, and the pre-pass's row when it built one of the pre-pass's or the record path's
pipelines (`ends_clean`). The start without the pre-pass that its first end gives builds none, so
it leaves that row as it was, and the next start that compiles them and ends there again is the
second. A restart after a crash report counts nothing.

The background mark (Android and iOS only: the desktops send no lifecycle events, so an end there
in the background counts): while the activity file records, the app going to the background (the
lifecycle's `SDL_EVENT_WILL_ENTER_BACKGROUND` on Android and iOS, `lifecycle_watch`, or Android's
`onTrimMemory` at level 20 or more, which arrives on the activity's thread even when the main
thread is held) writes `logs/startup-activity-background.txt` (the version, `background: the app
went to the background at pipelines 201 of 527 of the start-up set` or `with <n> pipelines made`,
then every `system: ` line that comes while there) and logs `ico_pc: the app went to the background
at pipelines 201 of 527 of the start-up set; an end there is not counted as one while preparing the
graphics`; coming back deletes it (`ico_pc: back in the foreground; the background mark is
deleted`), and so does the end of the recording. The mark is set under the log's lock, so when
both signals come at once (the lifecycle watch and `onTrimMemory(20)` on the Java thread) only the
thread that set it writes the text and logs. The app's state is kept whether the file records or
not: when the recording starts with the app in the background (Home while the renderer starts,
seen by `onTrimMemory(20)`), the mark is written then (`background: the app was in the background
before preparing the graphics began (with 0 pipelines made)`, logged the same way with `; an end
there is not counted as one while preparing the graphics`). The lifecycle watch itself is added
after the renderer starts, so a Home press before that is seen only through Android's
`onTrimMemory(20)` (not on iOS). The next start reads and deletes the mark: an end
while preparing the graphics with it is logged as `ico_pc: the previous run ended while: <note>, in
the background (the app went to the background at ...); the system ends apps there, so it is not
counted as an end while preparing the graphics: the start-up set as usual`, and the mark's system
lines the activity file did not get as `ico_pc: the previous run was told by the system in the
background: <line>` (`ico_diag_startup_activity_background`). A mark beside any other note (a lazy
run ended between two creations, a restart after a crash report) is logged as `ico_pc: the
previous run was in the background when it ended (<line>)` and decides nothing.

`ico_diag_startup_end_persist` takes a rule that already holds for a kind (`table`): nothing is
then written and the count is cleared, with `ico_pc: the previous start ended while preparing the
graphics (<note>); nothing is counted or written to <path>: <rule> (<table>)` or, for the second,
`ico_pc: the second start in a row ended while preparing the graphics (<note>); <setting> is not
written to <path>: <rule> (<table>)`. The window passes none for the pipelines and the pre-pass
(the device table's careful set is not the lazy one the setting writes) and `[dev] gpu_robust` for
the losses when the player set it (`port/platform/test/diag_test.c` keeps the rule's lines
tested).

A batch's note, and any note naming a pipeline of the pre-pass or the record path, is written
through to the disc (fsync, `act_sync_note`) before the driver starts on it, and in a lazy run
every first-use pipeline's note, so it is still there after a device that froze was rebooted
(issue 107). Each batch costs one sync: its `compiled: ` line (below) is written without one and
goes to the disc with the next note's sync (`act_write_sync`, `act_unsynced`). While the set is created the heartbeat line ends with the note
in flight (`| creating pipelines ...`), so a log that stops mid-set says what the driver was
compiling; a VU batch's note names each vertex shader with its words and SPIR-V instructions and
ends with the device's compiler (`; compiler E031.41.03.47`). Each finished batch adds a
`compiled: ` line after the note (the last 24 lines, written at once, so on the disc with the next
batch's note): its range, kind (2D, or VU with its variant), compiler, pipeline count, the compile
times' minimum, maximum and mean, the batch's wall time and threads, the process's memory and the
pipeline cache's size after it, the policy under a careful set (`; policy batch 8, threads 1,
budget 128 MB, cache each batch`), then a VU batch's times by vertex shader with its words and
instructions or a 2D batch's slowest pipeline with its modules; a run without the set adds its lazy
batches' lines the same way ([RENDER.md](PIPELINES.md#the-start-up-set); `rd.h` `rd_set_batch_fn`,
`diag_host.h` `ico_diag_startup_activity_batch`). The next start logs each as `ico_pc: the previous
run (version V) compiled: VU batch 297-328 of 702 (plain, compiler ...): 32 pipelines 0.20-0.90 s,
mean 0.41 s, ...` before the note it ended in, then the last one again as `ico_pc: the previous
run's last finished batch: <line>`, so a device that froze shows where its compile times stood
against the shaders' sizes (issue 107) and a start the system ended shows its memory batch by
batch (issue 93); the log itself has the line as each batch completes, with every pipeline's time
(`rd: pipelines: compiled ...; each (s): ...`). On Android the system's request to free memory
(`onTrimMemory`, which `IcoActivity` hands to `nativeTrimMemory` with its level before SDL turns it
into `SDL_EVENT_LOW_MEMORY`, which has none) is logged at once from the activity's thread: `ico_pc:
the system asked the app to free memory: low memory (onTrimMemory level 15, running critical;
signal 1 of this run) at pipelines 201 of 527 of the start-up set; memory 102 MB (most 194 MB);
device 320 MB available of 7632 MB` (`with <n> pipelines made` outside the set). The memory is
`ico_diag_memory_text`: the process's resident size and peak (`/proc/self/status`), then the
device's `MemAvailable` and `MemTotal` from `/proc/meminfo` (`ico_diag_device_memory`; `device
memory unknown` when that file cannot be read), the same text the heartbeat ends with, the window's
10 s line (`window: memory: ...; the process holds <n> MB (most <m> MB); device <a> MB available of
<t> MB; pipelines ...`) and report.txt's `memory: ` line carry. Only levels 5, 10 and 15 (`TRIM_MEMORY_RUNNING_*`:
the device is low on memory while the app runs) are called low memory; level 20 is `the app's
screen was hidden` (`TRIM_MEMORY_UI_HIDDEN`) and 40, 60 and 80 `the app is in the background`
(its place in the system's list of cached processes; Android 14 and later send 20 and 40 only),
which say the app left the foreground, not that memory ran short. The main thread's next poll (the start-up screen's
progress, the window's pump: `ico_diag_startup_activity_poll`) adds it to the activity file as a
`system: ` line among the `compiled: ` ones, and the next start logs it as `ico_pc: the previous
run (version V) was told by the system: low memory ...`, so an end the system chose shows whether a
low-memory signal came before it (`port/platform/diag_host.h` (`ico_diag_note_low_memory`);
`port/platform/android/host_android.c`). The activity note holds 640 bytes. The driver compiles the set on worker threads
(`rhi_create_pipelines`, [RHI.md](RHI.md#the-interface)); the main thread waits in the renderer's
progress function, about every 50 ms, and from it shows the "Preparing graphics (N of M)" screen on
every platform, drawn only once the set has taken 250 ms and then at most ten times a second, with
the time left from the rate so far under the bar once 3 s and 8 pipelines have passed ("About 2
minutes left") and, on Android, "This happens once after an update". It takes the window's events
each time it draws, so Windows does not mark the window "Not Responding" and Android does not offer
to close the app on a cold driver cache; a quit or close request, or a lost device, stops the set
after the batch in flight and the run ends (`port/platform/window_host.c` (`pipeline_progress`,
`pipe_estimate`, `progress_present_notes`)). While the set runs the heartbeat line ends with
`pipelines N of M, longest 1.3 s`, the count and the longest single compile so far
(`port/platform/diag_host.h` (`ico_diag_set_pipelines`)). A set that was not stopped is then
written to the Vulkan pipeline cache file at once (`rhi_pipeline_cache_save`,
[RHI.md](RHI.md#vulkan)), not only at a clean exit, so the "once after an update" holds
when the system ends the app or the device is lost later. A crash on a worker thread inside the
driver is reported as any crash, with the batch's note as the `while:` line, so the restart steps
treat it as a driver crash while preparing the shaders; the fixed-blend step reads the batch's
`dst` field, which every key of the batch shares (`port/platform/startup_retry.h`
(`startup_retry_note_has_dst`)).
A device loss adds a third line, `loss dump: <zip>` (or
`graphics device lost, no loss dump`), written at the loss and kept through the run's end whatever
the note and the mode, so the next start finds it even when the process is ended after the loss
(`ico_diag_note_device_loss`). That start logs `ico_pc: the previous run lost the graphics device;
its loss dump is <zip>` and, for a dump of the same version, runs the loss bisect once before the
game ([RENDER.md](RENDER.md#the-loss-bisect); `ico_diag_loss_bisect`, the bisect itself a
callback, `port/platform/window_host.c` (`window_loss_bisect`)): the file's note says the bisect is
running while it runs (`replaying the lost frame pass by pass (the loss bisect)`), the result is
logged as `ico_pc: loss bisect: <line>`, then `ico_pc: loss bisect: done; the game starts`, and the
file is deleted. A start ended inside the bisect leaves that note; the start after logs it, is not
lazy for it and does not bisect again. A loss of another version is logged, not replayed. ctest
`diag` runs these steps with the bisect stubbed (a lost pass, a clean frame, a bisect that could not
run, a start ended inside it), the loss noted on a run at the loss restart with the relaunch stubbed,
a lazy run's loss and a killed run's (`port/platform/test/diag_test.c` (`activity_child`,
`bisect_stub`)).

When the first level ends on a pass that drew, the second level narrows it to the draw in the same
start ([RENDER.md](RENDER.md#the-loss-bisect); `ico_diag_loss_bisect2`, the step a callback,
`port/platform/window_host.c` (`window_bisect_step`)). Its state is a file of its own,
`logs/loss-bisect.txt`, not the activity file:
```
ico-pc loss bisect 1
version <the program's version>
zip <the loss dump>
pass <P> draws <N> passes <T>
starts <k>
step <i> <kind> <first> <last> <filter> <arg> <lost|clean|killed|-> <ms> key <valid> <vu> <skinned> <vs> <clip> <tex> note <text>
```
one `step` line a step, written before the step runs with `-` for its verdict and again with the
verdict after it; the file is replaced whole each time (a temporary file renamed over it). The next
start reads it as untrusted: lines past 1100 bytes, numbers out of range, a step out of order, a
kind or filter it does not know, or another version's file delete it (logged); other lines are
ignored. A step without a verdict becomes `killed` (logged as `ico_pc: loss bisect: continuing at
step 7 (start 2 of 6; step 6 ended the run)`); a step whose renderer did not start is planned again
at the next start. The file is deleted when the plan ends (16 steps at most), after 6 starts (`ICO_DIAG_BISECT2_STARTS`;
a draw early in its pass loses the device on nearly every prefix step, about 12 lost steps with
the masked one, so the alone and class steps need a fifth start), or
when the dump cannot be replayed; a start spends at most 90 s on steps and leaves the rest to the
next, and it runs at most 3 steps that lose the device: after the third, `ico_pc: loss bisect: 3
lost steps in this start; step 5 waits for the next start`. With the first level's loss a start
then loses the device at most 4 times, under the 5 graphics resets in 60 s after which Windows
restarts the whole system (its `TdrLimitCount` and `TdrLimitTime` defaults); the bound holds on
every platform, and the state file carries the rest to the next start, within the 6. While a step runs the activity file's note says so (`replaying the lost frame draw by draw
(the loss bisect)`); a start after one ended inside a step logs it, is not lazy for it, and the
second level continues. An unfinished second level goes before a newer loss dump, which stays in
logs. ctest `diag` runs it with the step stubbed (`actbisect2`, `ICO_TEST_BISECT2`): the draw
found in 4 starts of at most 3 lost steps each, draw 1 (every prefix step lost) found with its
classes in 5 starts, a start killed at step 3 continued at step 4, a renderer that did not start,
the 16-step and 6-start bounds, the time budget, and state files that do not parse or are another version's
(`port/platform/test/diag_test.c` (`bisect2_stub`)).

A heartbeat and a watchdog share one OS thread of their own, so a game thread that spins without a
kernel call cannot stop them. Every 2 s the heartbeat logs one line of progress
(`port/platform/trace_host.c` (`ico_host_status`: vsyncs, ticks, stage, the disc and
[SIF](ARCHITECTURE.md#terms) counters)), ending with `| memory <n> MB (most <m> MB); device <a> MB
available of <t> MB` where `/proc` exists (`ico_diag_memory_text`; `port/platform/diag_host.c`
(`memory_note`)). The watchdog stops the run, with every thread dumped and
exit code 4, when no Main tick comes `watchdog=` seconds after boot (default 30) or no new tick or
other progress for twice that; a movie notes each picture as progress, time inside a present is
excused for an effects program, and the first limit is doubled when one is loaded
(`port/platform/diag_host.h` (`ico_diag_start`, `ico_diag_note_progress`);
`port/platform/main_host.c` (`start_watchdog`)).

On Android the kernel GPU driver's nodes are sampled on a thread of their own, never this one, and
the app's Java side reports the thermal state and screen recording (issue 123):
[ANDROID_DIAGNOSTICS.md](ANDROID_DIAGNOSTICS.md). The thermal asks (JNI calls into
PowerManager) and the memory line's `/proc` reads run once a second on the log thread, not on the
game thread; the 10 s lines read their last answers from lock-free cells
(`port/platform/sys_cell.h`, `window_stall.c`). The device line at the window's start, `window:
device: Android 16 (API 36), Google Pixel 10 Pro Fold (...)`, comes from the system properties.

**A wait the system holds.** The hang verdict applies to the game's own code only. The window build
marks the main thread's calls into the system that run none of the game's code (`SDL_PollEvent`, and
on Windows `SDL_UpdateJoysticks`) with `ico_diag_os_wait_enter`/`ico_diag_os_wait_leave`, and the time
inside them counts towards neither limit. A wait that outlasts the limit in force is named once:

```
ico_pc: watchdog: the window's event pump has not returned for 60 s (the system is holding it: the game is not hung); waiting
ico_pc: watchdog: in the system's messages and SDL's own work (SDL_PollEvent), handling WM_xxx(0x....) for 59.8 s; last messages before it: ...; SDL events before it: ...
```

then `ico_pc: watchdog: the window's event pump returned after N s; the game goes on` when it comes
back, and the heartbeat carries `| in the window's event pump N s` while it lasts (outside its
"no progress" comparison). The run still ends with the watchdog's report when one wait passes
`[dev] watchdog_pump` seconds (default 600, 0 no limit), so a deadlock in the system is reported; a
system sleep inside a wait counts towards neither. The bound holds only while a watchdog limit does:
with `[dev] watchdog = 0`, and on Android before the game starts (the diagnostics thread runs from
the start, `ico_diag_start_reporter`, while the start-up set's progress screen pumps the window's
events), a wait never ends the run, and time in the background pauses the watchdog there too
(`lifecycle_watch` before the window loop is open), so Home pressed on "Preparing graphics" for an
hour does not end the app (`port/platform/diag_host.h`
(`ico_diag_os_wait_enter`, `ico_diag_set_os_wait_limit`, `ico_diag_set_os_wait_describe`)). The case
behind it: a Windows player's game, paused and left alone, had its pump held for 50 s three times in
a row right after a controller turned itself off, and the 60 s limit ended a run that was waiting on
Windows, not hung. ctest `diag` checks a 3.5 s held pump against a 1 s limit (no end, the line
once), a stall in the game's code right after it (the end), the bound, and the bound with no limit
in force (`pump`, `pumphang`, `pumpbound`, `pumpearly`, `pumpoff` in
`port/platform/test/diag_test.c`). Milestone lines carry the wall time, vsyncs, Main
ticks and the current thread (`ico_diag_milestone`), and game threads are named by the function
`iosThreadCreate` runs (`ico_host_thread_func`). `ico_diag_float_fault` logs a NaN that reaches game
code the PS2 never fed one (`port/platform/diag_host.h` (`ico_diag_float_fault`)).

**Loads, stage switches and story flags (issue 160).** A save's load logs one milestone after it
restores the flags it keeps across `gflagInit` (`ico2/common/src/layout_action.c`
`la_host_load_log`): `load: slot 0, stage 41; kept flags (id=in the save/kept) 355=0/0 351=0/0
350=0/0 352=0/0 349=1/0; new game flag 349`, each disc's own five ids (the fifth is its new-game
flag; the twenty words the walk reads per disc are checked by ctest `keep_flags` and its six disc
runs). Every stage-switch request (`ico2/common/src/StageManager.c` `stgmgrForceSwitch`,
`stgmgrForceSwitchWithFadeColor`, through `ico_host_stage_switch`) logs `stage switch request:
stage S, fade in F out G, cmd C, by thread #N name` with the milestone prefix (the first 200, then
every 10th), and a request made while the stage manager's queue still holds an untaken one adds `a
second stage switch request while one is queued (stage A then B, N queued)`: the retail game can
ask twice in one Main tick (a Continue's load and the title's new-game wait did, and the stage
loaded twice). `gflagChk`, `gflagOn` and `gflagOff` (`ico2/script/src/gflag.c`) log an id at or
past the disc's flag count (`ico_region_gflag_count`, the region table's `gflag_count`) as `gflag:
flag ID read|set|cleared past the disc's COUNT flags (from CALLER)`, rate-limited; the game's
access goes ahead unchanged.

**A GPU hang.** When the watchdog fires (either limit; not the pump's bound) while the main thread
is blocked in a wait for the GPU and the work it waits for has not finished
([RHI.md](RHI.md#device-loss), `rhi_gpu_progress`: the wait's value above the newest seen finished),
the driver neither finished the picture nor reported the device lost, and the end is the device
loss it would have been. The WATCHDOG block's next lines say so in place of the "waited for a
picture" line:

```
ico_pc: WATCHDOG: no new Main tick for 60 s (the last was tick 127, progress 0)
ico_pc: the graphics driver did not finish the picture (submission 262 has not finished after a 60 s wait)
ico_pc: the GPU: the main thread has waited 60 s for submission 262; the newest is 263 (replay 249, frame 127), the newest seen finished 261; this end counts as a device loss (the loss dump; the next start counts the loss and does not replay the frame)
```

Then come the main thread's sample, the heartbeat (which carries `| waiting for the GPU N s
(submission V; the newest S, the newest seen finished D)` whenever such a wait has lasted a second),
the threads and the symbols as for any watchdog end. On the watchdog thread the window then writes
what a device loss writes before its box: `window: the graphics driver stopped responding (a GPU
hang); what was being drawn follows, as for a device loss`, the index span, the renderer's
[pass ring](RENDER.md#the-pass-ring) and the [loss dump](RENDER.md#the-loss-dump), which notes the
loss in the activity file with the hang's own line, `loss dump (the driver hung): <zip>` (without a
dump, `graphics driver hung, no loss dump`). The next start counts it in the row of losses (on
Android, robust access for that start), logs `ico_pc: the previous run ended because the graphics
driver stopped responding (a GPU hang, counted as a device loss); its loss dump <zip> is not
replayed (the replay could hang the same way)`, and a report it writes attaches the zip; it does not
run the [loss bisect](RENDER.md#the-loss-bisect) on the dump: the bisect replays the frame before
the watchdog starts and waits for the GPU with no limit, so on a driver that hangs at that frame the
start would freeze for good (`port/platform/diag_host.c` (`ico_diag_note_device_loss`,
`ico_diag_startup_activity_begin`, `loss_hang`)). All of it is CPU data the main thread wrote and
nothing else writes while it is blocked; no call goes to the hung device
(`port/platform/window_host.c` (`window_gpu_hang`, `window_gpu_progress`)). The
[report file](#the-report-file)'s `what ended the run:` is `the graphics driver stopped responding
(submission V has not finished after a W s wait)` (V the submission the main thread waits for, W how
long it has waited), and the box is the device loss's: on a computer
"The graphics card stopped responding while the game was drawing, so the game has to close. Your
last save is kept; the log says why.", the overclock advice and the report file to attach; on a
phone the same first sentence, "Start the game again." and the report file. The run ends
`ico_pc: the run ended: the graphics driver stopped responding (watchdog)` with the watchdog's exit
code 4 and no restart: the loss restart runs at exit after the normal quit path, which the blocked
main thread never reaches, and a process holding a hung device should not start a second one
before the driver lets go of it. A watchdog end with the main thread inside a present but in no
wait for the GPU (the driver's acquire or present call has not returned) stays a freeze, with one
more line naming the GPU's state: `ico_pc: the GPU: no wait for it in progress; the newest
submission is S (replay R, frame F, made N s ago), the newest seen finished D`. One start line says
whether the backend tells a hang apart: `window: GPU hang: the watchdog's end while the game waits
for the GPU and the work has not finished counts as a device loss (the loss dump, the next start's
loss steps)` on Vulkan, `window: GPU hang: not told from a freeze on this graphics backend ...` on
Direct3D 12 (`port/platform/diag_host.h` (`ico_diag_set_gpu_hang`, `IcoDiagGpu`);
`port/platform/diag_host.c` (`watchdog_fire`, `gpu_state`, `gpu_hung`)). ctest `diag` fakes the
backend's progress (`hang`, `hangnodump`, `hangnot` in `port/platform/test/diag_test.c`): the
lines, the dump before the report, the report's line, the box, the activity file's loss and the next
start's count, the loss without a dump, and the freeze with no wait in progress.

**The GPU hang limit** (issue 123: a minute of black screen before the box on an Adreno
740). The same end comes as soon as one wait for the GPU has lasted the limit, half the watchdog
(`watchdog = 30`: 15 s; at least 2 s; `watchdog = 0` turns both off), not after the later limit's
2 x `watchdog` without a Main tick. The watchdog's thread polls the backend's progress four times a
second and counts one wait for one unfinished submission the way it counts its limits (not while
paused in the background, not across a suspend; only while a limit is in force), and at the limit
runs `watchdog_fire` with the reason `the GPU: submission V not finished after W s (limit L s); the
newest is S, the newest seen finished D`; the lines, the dump, the loss line, the report and the
box are the GPU hang's above. Both ends run on the watchdog's thread and `watchdog_fire` runs once,
so the first wins. The waits themselves are unchanged: the main thread blocked in the driver is not
the one that ends the run, so no backend wait was bounded for it. Only a backend that tracks its
submissions (`rhi_gpu_progress`: Vulkan) has the limit; Direct3D 12, Direct3D 11 and OpenGL track
none (on Windows the system resets a GPU that stops for 2 s and the backend sees the device
removed). The host sets it before the window opens (`port/platform/main_host.c`
(`set_gpu_hang_limit`): `ico_pc: GPU hang limit 15.0 s (half the watchdog's 30 s, at least 2 s; 0 =
off): ...`, and `ICO_GPU_HANG_LIMIT` for the Vulkan backend's loss diagnostics line, `...; hang
limit 15 s`); the diagnostics' start line names it (`GPU hang limit 15.0 s for one wait for the GPU
(0 = off)`). The arithmetic and the watch are `port/platform/gpu_hang.h` (ctest `gpu_hang`); ctest
`diag`'s `hanglimit` ends a faked wait at a 2 s limit before a 60 s later limit. On Android, when the
start runs on the device's own Qualcomm driver (no driver package), the hang's box and the device
loss's end with one more sentence in the menu's language, the Options > Graphics driver > Add a
driver route to a Turnip package (`UI_STR_GPU_HANG_TURNIP`;
`port/platform/android/gpu_driver_android.c` (`ico_gpu_driver_android_hang_note`), registered with
`ico_diag_set_gpu_hang_note`).

With the [render thread](PLATFORM.md#the-render-thread) presenting, a picture the driver never finishes
leaves the render thread inside the driver's present and the main thread waiting for the render
thread, not for the GPU, so the backend's progress names no wait. The render thread's heartbeat
covers it: the watchdog's thread reads the picture in progress without a lock
(`port/platform/diag_host.h` (`ico_diag_set_render_heartbeat`); `port/platform/render_thread.c`
(`rt_heartbeat`)) and counts it through `IcoRenderHangWatch`, the same limit and the same
counting rules (not with an effects program loaded, whose first presents prepare its effects).
Only pictures are counted (a call between pictures, such as the targets made again, runs under the
watchdog's own limit, as it did on the main thread), and the driver's pipeline creations are
progress: the count starts again whenever one begins or ends and stays at 0 while one is in
progress (`port/platform/gpu_hang.h` (`ico_render_hang_step`), fed the activity note's counter),
so a picture that makes many pipelines on first use (about 250 ms each on a phone) is not a hang,
and one creation that does not return is left to the pipeline limit and the watchdog; at
the limit the WATCHDOG line is `the render thread: picture P has not finished its submit and
present after W s (limit L s); the last finished D`, its second line `the graphics driver did not
finish the picture (the render thread has been inside picture P's submit and present for W s; the
main thread waits for it); this end counts as a device loss (...)`, and the rest (the dump, the loss
line, the report's `the graphics driver stopped responding (the render thread's picture P has not
finished after a W s wait)`, the box) is the GPU hang's (`port/platform/diag_host.c`
(`watchdog_loop`, `watchdog_fire`, `render_hang`)).

**The pipeline limit (Android).** A pipeline creation that does not return (issue 161: the
Retroid Pocket 6's Adreno 740 stayed in one for 18 s while the device's memory fell) ends the run
in order, with the GPU hang limit on: while the activity note names a creation in progress and
neither the note, the set's count nor the background set's returned compiles move, the watchdog's
thread counts the time (`port/platform/gpu_hang.h` (`ico_pipe_stall_step`)), and at the limit logs
`ico_pc: the graphics driver has not returned from creating a pipeline for N s (pipelines D of T;
the background set's compiles: R returned, L not; the limit X s before the game starts|in play[,
<serial reason>][; inside the start's self-tests]): the run ends now, in order, <next>`, writes
the report, the stall mark (the plain VU shaders for every later start of the version) and the box,
and exits; the activity file keeps the note, so the next start counts the end (the careful set,
then the set on first use, [PIPELINES.md](PIPELINES.md#the-start-up-set); a family the note alone
names left out, `[dev] pipeline_skip`). The limit (`ico_pipe_stall_limit_s`): 10 s before the game
starts (the self-tests and the start-up set), 60 s there on a driver whose compiles run one at a
time (`RhiLimits.serialPipelines`, a PowerVR B-Series driver, issue 188, set by the window with the
floor's driver class, `ico_diag_set_pipe_compiler_serial`, logged once: `ico_pc: the pipeline limit
before the game starts: 60 s (PowerVR B-Series: its compiles run one at a time), 30 s in play`),
30 s in play on every driver. On every driver the time does not count while the process's
resident memory grew by 2 MB or more since the last heartbeat (`ico_pipe_mem_moving`: the
compiler at work; the issue 188 phone went 147, 251, 275 and 296 MB at its heartbeats before it
stood still), with a line while a creation is held so (`ico_pc: a pipeline creation in progress,
the process grew N MB since the last heartbeat (the driver's compiler at work): the pipeline limit
waits (K times; H s held of at most C s)`, the first 8, then every 100th). The hold has a cap
and a floor (`ico_pipe_mem_hold`): it ends after three times the limit held in one creation (30 s
before the game starts, 180 s on a serial compiler, 90 s in play; the limit then runs, so such a
creation ends the run at four times the limit at the latest), and it never holds while the free
memory is under the floor or the system has sent a low-memory level (`ico_diag_background_memory`,
read by the watchdog at every poll inside the creation: the set's own stops between batches
cannot interrupt a call inside the driver, and a call that never returns but keeps growing would
otherwise be ended only by Android's low-memory killer, with no box and no stall mark); a growth
not held says `... but the pipeline limit does not wait for it (<the floor's figures|the
system's low-memory signal (<level>)|held for its longest already>; K times)`. `<next>` and the box say the next
start prepares the graphics another way only when it does (`ico_pipe_stall_next_differs`): not
when the creation was inside the blend and VU pre-pass self-tests, which run first at every start
whatever the set (the window tells the watchdog, `ico_diag_set_selftest`), nor on a run already
making its pipelines on first use, unless the note names only a family the renderer can leave out
or a VU shader while the plain VU shaders are not yet in force: then `and the next start prepares
them the same way (the self-tests run first at every start)` or `(this run made them on first use
already)`, and the box asks for a report if it happens at every start (issue 188: every start
ended the same way inside the self-test while the box promised another way). At a start the
self-tests keep the set's rule for the start-up screen (`Preparing graphics`): nothing is drawn
while they are quick (every desktop start, a warm phone start), and the screen is drawn between
the blend and the VU pre-pass self-tests once they have run 250 ms (the window's thread is inside
the driver while a self-test's pipeline compiles, so it cannot draw sooner, and never inside a
self-test, whose frames hold the renderer's state under test); Apply's switch draws it at once
(`Changing the graphics`: the old picture is gone there). On Android its note `This happens once
after an update that changes the graphics` is shown only before a set (the careful or full start),
not on a lazy start or a switch. The line `window: start: the blend and VU pre-pass self-tests
took N ms; the start-up screen drawn|not drawn (the blend self-test took under 250 ms)` says
which (`port/platform/window_host.c` (`selftests_screen`, `selftests_between`)).

The window build logs a stats block every 10 s. A host step longer than `[dev] slow_step_ms`
(default 8, 0 off) logs a slow-step line ([DISPLAY.md](DISPLAY.md#pacing);
`port/platform/window_host.c` (`slow_step`); `port/platform/host_loop.h` (`IcoStepProfile`)).

**The event pump's long calls.** Each part of `ico_window_pump` over 1 s logs a line of its own:

```
window: the event pump's PART took N ms at vsync V (Main tick T); last messages before it: WM_TIMER(0x0113) x3 0.4 s ago, WM_DEVICECHANGE DBT_DEVICEREMOVECOMPLETE(0x0219) 50.3 s ago, ...; SDL events before it: gamepad removed 50.3 s ago, ...
```

PART is the system's messages and SDL's own work (`SDL_PollEvent`), on Windows the controllers'
scan and update (`SDL_UpdateJoysticks`), the handling of one event, the settings (`video_apply`),
the captures and the pointer, or the pads' reading and rumble. On Windows SDL's own joystick update
is taken out of `SDL_PollEvent` (`SDL_HINT_AUTO_UPDATE_JOYSTICKS` = 0 before `SDL_Init`) and called
at the pump's start (and on the start-up screens), so the two are timed apart: after a device
change SDL 3.4 enumerates the DirectInput devices on the thread that pumps
(`SDL_windowsjoystick.c` `WINDOWS_JoystickDetect`, `SDL_dinputjoystick.c` `SDL_DINPUT_JoystickDetect`,
whose own comment says `EnumDevices` can take a minute). The messages come from SDL's message hook
(`SDL_SetWindowsMessageHook`: the posted ones, for every window of the main thread, SDL's hidden
device-notification windows included) and a window procedure in front of SDL's on the game window
(the sent ones, which never pass the hook: `WM_SYSCOMMAND`, `WM_POWERBROADCAST`,
`WM_DISPLAYCHANGE`, `WM_ACTIVATE`...); both let every message through unchanged, and a message whose
handling takes over 1 s logs `window: the window message NAME took N ms in SDL's window procedure`.
The last 16 messages and 8 SDL events are kept, a repeat of the newest counted (`x3`), mouse moves,
raw input and hit tests left out; the names cover `WM_SYSCOMMAND` `SC_SCREENSAVE` and
`SC_MONITORPOWER` (on, low, off), `WM_POWERBROADCAST`, `WM_DEVICECHANGE`, `WM_DISPLAYCHANGE`,
`WM_ACTIVATE`, `WM_NCACTIVATE`, `WM_SETFOCUS`/`WM_KILLFOCUS`, `WM_SIZE`,
`WM_DWMCOMPOSITIONCHANGED`, `WM_INPUT_DEVICE_CHANGE` and `WM_TIMER` among others, and an unknown id
as `message 0x....`. The window's focus, visibility and display events log `window: system event:
NAME at vsync V`; a joystick that arrives or leaves logs `window: system event: joystick added at
vsync V: "Xbox Controller" (GUID 03000000..., XInput)`, recorded with its name, GUID and the SDL
driver its GUID names (byte 14: `x` XInput, `h` HIDAPI, `r` Raw Input, `w` Windows.Gaming.Input,
`g` GameInput, 0 DirectInput on Windows; a removal named from its arrival), and every slow-pump line
and the watchdog's description end with `controllers before it: removed "..." (GUID ..., XInput)
50.3 s ago, ...` (the last 4). The 10 s line carries `pump max N ms`
(`port/platform/window_host.c` (`pump_poll`, `pump_joysticks`, `pump_slow`); `port/platform/pump_watch.h`;
`port/platform/window_pump_win.h`).

**What settles issue 105.** The standing hypothesis is that the 50 s holds are SDL's DirectInput
enumeration after a controller change. The next log decides it four ways:

- A slow controllers' scan adds `window: the controllers' scan: N controllers after it; the HID
  device list changed K times during it (SDL_hid_device_change_count a -> b); the last controller
  arrived or left T s before it ended; the DirectInput scan is on|off`.
- SDL's own log: the window installs `SDL_SetLogOutputFunction` before `SDL_Init` with the INPUT
  and SYSTEM categories at VERBOSE, so the joystick drivers' own lines (a HIDAPI device added or
  removed with its path and driver, DirectInput's warnings) are kept in a ring of the last 64
  (`pump_watch_sdl_log`); every slow-pump line is followed by the ring's lines from 120 s before the
  part began (`window: SDL's own log from 120 s before it to its end (N lines, oldest first):`, then
  `window:   12.3 s ago INPUT debug: ...`), and the report file's `report.txt` ends with the whole
  ring (`ico_diag_set_report_extra`). SDL's info, warning, error and critical lines also go to the log as
  `window: SDL <category> <priority>: ...`, as SDL's own output wrote them before. Info and warning
  lines each go through the rate limiter under a key of their own (`window: SDL's log, info`,
  `window: SDL's log, warnings`: the first 8, then every 100th), so a flood of one cannot silence
  the other; error lines go through one of their own with a high ceiling (`window: SDL's log,
  errors`: the first 64, then every 1000th, `ico_log_limited_n`), so a flood cannot grow the log
  without end and the run's end line still counts them; critical lines are never limited
  (`sdl_log_output` in `port/platform/window_host.c`).
- On Windows the watchdog samples the held main thread at 2, 10 and 30 s into a held wait:
  `ico_pc: watchdog: the window's event pump has held the main thread for 2 s: at win32u.dll+0x...;
  code on its stack (innermost first): DINPUT8.dll+0x..., SDL3.dll+0x..., ico_pc_x64.exe+0x...`.
  Each stack word that points into executable image memory is named from the module's own export
  table (no loader lock, which the held thread may own: a device scan loads drivers), so
  `DINPUT8.dll` on the stack names the DirectInput scan and `hid.dll`, `setupapi.dll` or
  `cfgmgr32.dll` the HID enumeration (`port/platform/diag_host.c` (`os_wait_sample`,
  `code_address`)).
- The test: `[dev] joystick_directinput = false` (exported as `ICO_JOYSTICK_DIRECTINPUT=0`) sets
  `SDL_HINT_JOYSTICK_DIRECTINPUT` to 0 before `SDL_Init`; SDL then never creates its DirectInput
  object and `SDL_DINPUT_JoystickDetect` returns at once. XInput, HIDAPI and Raw Input still find
  the usual pads; a device only DirectInput knows is not found. The summary line says `controller
  ... (DirectInput scan on|off)` and a line names the switch when it is off. SDL 3.4 offers no
  switch that moves the scan off the main thread: `SDL_HINT_JOYSTICK_THREAD` (on by default) only
  runs the device-notification window and its 300 ms and 2 s timers on a thread of its own
  (`SDL_windowsjoystick.c` `SDL_JoystickThread`); the scan itself stays in `WINDOWS_JoystickDetect`,
  which `SDL_UpdateJoysticks` calls on the pumping thread. So there is no `joystick_update` key.

If the hypothesis holds, the line names the controllers' scan, the stack sample shows `DINPUT8.dll`
and the holds stop with `joystick_directinput = false`; if the stack shows another module, or the
held part is `SDL_PollEvent` with no controller change before it, the cause is elsewhere and the
same lines name it.

**The display and the computer awake.** SDL's `SDL_DisableScreenSaver` sets
`ES_CONTINUOUS | ES_DISPLAY_REQUIRED` on Windows (`SDL_windowsvideo.c` `WIN_SuspendScreenSaver`) and
returns 0 for `SC_SCREENSAVE` and `SC_MONITORPOWER` while it is in force (`SDL_windowsevents.c`,
`WM_SYSCOMMAND`); the display flag alone does not keep the computer out of its idle sleep. The
Windows window build sets `ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED` from the main
thread at the first pump and every 30 s after, logged once (`window: the display and the computer
are kept awake while the game runs`), but only while the window has the input focus and is not
minimized (the caller passes it from `SDL_GetWindowFlags`: `SDL_WINDOW_INPUT_FOCUS` set,
`SDL_WINDOW_MINIMIZED` clear). When it loses focus or is minimized, `SetThreadExecutionState(ES_CONTINUOUS)`
is called once to release and `window: the display may sleep while the game is minimized or in the
background` is logged; when it is focused again the next pump sets the flags at once and logs
`window: the display and the computer are kept awake again`. Both lines go through the rate limiter
(first 8, then every 100th). A game left running in front never lets the display or the computer
sleep, as a console does; one left behind does not hold them (`port/platform/window_pump_win.c`
(`ico_pump_win_keep_awake`)).

**A lost graphics device.** When the backend reports the device gone (`rhi_device_lost`), the window
pump and the start-up screens' pump end the session through the normal quit path: one line
`window: the graphics device was lost; quitting`, then the renderer's
[pass ring](RENDER.md#the-pass-ring) (the last replays and the passes of the last three, with the
sequence numbers the checkpoint markers carry and each effect's name from `rd_perf_post_name`), a
flush of the log, the [loss dump](RENDER.md#the-loss-dump) (`logs/loss-<date>-<time>.zip`, logged as
`window: loss dump: frames N (the lost one) and M (the one before) written to <zip> in T s`, and noted in the activity
file), the [report file](#the-report-file) (which holds the loss dump and the log), and one message
box: the card stopped responding, the game has to close, the save is kept, "If the graphics card is
overclocked, try its stock clocks first.", and a request to attach the newest file in logs that
starts with report-, naming it (without a report, the loss dump's file name and a request to attach
it with the log); on a phone the box asks the same. The loss restart above comes after the loss dump, but a
restarted run does not bisect it: its step changed the blending or the backend, so its replay would
not be the path that lost the device, and a loss it reproduced would add a GPU reset to the
restart chain. It logs `ico_pc: loss bisect: not run (this start changed the blending or backend
after the loss); the loss dump stays in logs: <zip>`, and the zip stays for `rd_replay_tool
--loss-bisect`. Only a start at no restart step bisects. The box is modal:
the watchdog is paused before it (`ico_diag_watchdog_pause`, so the time the player takes to read
it is not reported as a hang), the heartbeat stops, and after it one line
`window: the message was closed after N s` and the watchdog running again, so a hang in the shutdown
that follows is reported, before the session ends
(`port/platform/window_host.c` (`device_lost_quit`)). The backend's own loss report comes before
it, at the first failing call ([RHI.md](RHI.md#device-loss)).

`port/platform/assert_host.c` is newlib's `__assert` as the game calls it: the same message on
stderr, then `abort`, which the crash handler reports.

### The last-run file

`logs/last-run.txt` says where a run was when it ended without a word: the system ending the app,
a driver ending the process, anything that leaves no crash line, device loss or watchdog end
(issue 215). It is written on every platform from the start of the window's start-up
(`ico_diag_startup_activity_begin`) to the end of the run (`port/platform/last_run.h`,
`last_run.c`). The file has a fixed size and layout: one line a field, each in a slot of its own
width at its own offset, `<field>: <value>` padded with blanks and ended by a check of the slot's
bytes (`[3fa1c2d0]`, 32-bit FNV-1a with the slot's index mixed in). It is rewritten in place with
one positional write of the whole file at every heartbeat (2 s, not synced: a process the system
kills leaves the system's page cache, which still reaches the disc) and at every phase change
(synced, about ten a run); the background switch and the system's low-memory signal write it
without a sync and only when no other write is in flight (Android calls them on its UI thread,
which must not wait on the disc; a skipped write is the next heartbeat's). A write the process did not finish leaves every slot whole, from one
write or the other, or failing its check, which the reader marks as such: never a mix.

| field | value |
| --- | --- |
| `version` | the version and commit |
| `pid`, `started` | the process id; the start in UTC |
| `seconds since start` | at the write |
| `phase` | `starting: <the start's last step>`, `the start-up set: pipeline <n> of <m>`, `a self-test: <its step>`, `the game starting`, `the first picture`, `play: stage <n>, Main tick <t>` (the tick of the last heartbeat), each with `(since <s> s)`; a step that does not change the phase (the set's own, a renderer switch in play) is added as `; the last step: <step>` |
| `app` | `in front` or `in the background`, since when |
| `last trim signal` | the last `onTrimMemory` level, its name, when, how many in all; `none` |
| `GPU resets` | `<a> at start, <b> now (+<d>)` from the kernel GPU driver's `reset_count` (Android's sampler, every 250 ms), else why not (`not readable (EACCES)`, `not available on this system ...`) |
| `free memory` | `MemAvailable` now, the run's lowest and when, the start-up set's free-memory floor (Android), then the system's split from the same read (issue 215, [where the memory is](#where-the-memory-is)): `; file cache <n>, anon <n>, kernel <n>, GPU <n or ?>, rest <n>, swap used <n> MB` (the GPU figure the last full read's: the heartbeat reads `/proc` only) |
| `process memory` | `VmRSS` now and `VmHWM` |
| `GPU memory` | this process's files in the kernel driver's `/sys/class/kgsl/kgsl/proc/<pid>/` (each read as its first integer and taken for bytes), read on the sampler's thread every 2 s; or why not |
| `pipelines` | the start-up set's count, the pipelines made as the frames needed them, the last pipeline note |
| `end` | `running (...)` until an end is written: `clean` (the window's clean close; `clean, after: <words>` after a loss), `crash (signal <n>)` (POSIX; `crash (exception 0x<code>)` on Windows) written by the crash handler with async-signal-safe calls only (a slot built by hand, one `pwrite`, a bounded wait for a heartbeat's write in flight), `graphics device lost`, `the graphics driver stopped responding (a GPU hang)`, `the watchdog: <reason>` |

At the next start the file is read before it is rewritten, copied to `logs/last-run-previous.txt`,
and each field goes to the log beside the activity file's lines:
`ico_pc: the previous run: <field>: <value>`, a field the file does not hold whole
`... not readable (the file ends before it)` or `(a write the run did not finish)`, and a run that
wrote no end `ico_pc: the previous run: end: none written: the run ended without a word (no clean
end, crash, device loss or watchdog end was written; the fields above are its last write, <s> s
after its start)`. Without the file the line is `ico_pc: the previous run: no last-run.txt (the
first start with it, or the file was deleted)` and an older copy is deleted. `last_run_parse`
gives the facts (`LastRunFacts`: pid, seconds, phase with stage and tick, background, trim level,
resets, free memory, the end and a crash's signal) for the verdict of the next start.

On Android 11 and later each phase change also keeps at most 128 bytes (`ico-pc <phase> | resets
47->66 | free 1911 MB, lowest 1897`), which the next heartbeat (within 2 s) hands from its own
thread, never from the game's fiber stack where a phase change can come and a Java call is not
made, to `ActivityManager.setProcessStateSummary`
(`IcoActivity.processStateSummary`, `port/platform/android/last_run_android.c`), which the system
keeps in the process's exit record (`ApplicationExitInfo.getProcessStateSummary`) even when the
app's files are lost; the first outcome is logged once (`ico_pc: last-run: the system keeps a
summary ...` or `ico_pc: last-run: no summary in the system's exit record: <why>`). At the
window's open Android logs `window: kernel GPU driver: this process's GPU memory: <folder>:
readable <name> (<value>), ...` once, and one line for the stall lines: `window: stalls: the GPU's
reset count (reset_count, <n> at the start) is sampled every 250 ms: the kernel GPU driver's line
around each stall gives its change across it`, or `window: stalls: the GPU's reset count is not
readable here (<why>): the stall lines carry no change of it`. Where both samples around a stall
read it, the kernel GPU driver's line around it ends with `; the GPU's reset count +<d> across it
(<a> before, <b> after)` (`port/platform/gpu_sysfs.c` (`gpu_sysfs_around_text`,
`gpu_sysfs_resets_note`)). Nothing here changes behaviour. Tests: `last_run`, `gpu_sysfs`, `report`.

### Where the memory is

Issue 215: on the Retroid Pocket 6 the device's free memory (`MemAvailable`) fell from 7249 to
1911 MB while the start-up set made 290 pipelines, and the process grew only 168 -> 260 MB; on the
issue 123 tablet the same fall came in the VU batches alone. `MemAvailable` cannot say what holds
the memory, so `port/platform/mem_breakdown.c` reads the system's own split and
`port/platform/mem_watch.c` prints it around the start-up set (Linux and Android; "not available"
elsewhere). Each call reads each file once (open, read to its end or the buffer's, close; no
allocation): `/proc/meminfo` (`MemTotal`, `MemFree`, `MemAvailable`, `Buffers`, `Cached`,
`SwapCached`, `AnonPages`, `Shmem`, `Slab`, `KernelStack`, `PageTables`, `SwapTotal`,
`SwapFree`), `/proc/self/status` (`VmRSS`, `VmHWM`, `RssAnon`, `RssFile`, `RssShmem`, `VmSwap`),
and the `/sys` nodes where readable: the kernel GPU driver's totals
`/sys/class/kgsl/kgsl/{page_alloc,vmalloc,coherent,secure}` summed (taken for bytes), the dma-buf
heaps' pools `/sys/kernel/dma_heap/total_pools_kb` and ION's `/sys/kernel/ion/total_heaps_kb`, and
zram's memory (`/sys/block/zram0/mm_stat`'s third field). A read cut inside a line keeps the
lines before it; a file that cannot be read keeps its errno for the sources' line. The heartbeat
(the watchdog's thread) reads `/proc` only and takes the last `/sys` figures another read made.

The memory in use (`MemTotal - MemFree`) is split into the file cache (`Buffers + Cached -
Shmem`), shared (`Shmem`), anonymous (`AnonPages`, every process's), the swap cache, the kernel
(`Slab + KernelStack + PageTables`), the GPU (the KGSL totals), dma-buf, zram, and the rest: what
is in use but in none of these. The rest is the remainder, so the parts sum to the memory in use;
a part not readable is in the rest (on a phone whose KGSL nodes are denied, the GPU driver's own
memory shows there), and two sources that overlap show as a rest below zero.

| line | when |
| --- | --- |
| `ico_pc: memory: the system's memory split read from: <paths>; not readable: <path> (errno <n>, ...)` | once a run, at the first full read |
| `ico_pc: memory before the start-up set (<m> pipelines): free <a> MB of <t> MB (MemAvailable; MemFree <f> MB); in use <u> MB: file cache <n> MB, shared ..., the rest <n> MB (in none of these); swap used <s> of <t> MB; this process <r> MB resident (...)` (Android adds `; this process's GPU memory: <the last-run file's GPU memory text>`) | the set's start (`ico_diag_set_pipelines` with a total after none) |
| `ico_pc: memory: the start-up set's batch lines carry the system's memory every <N> pipelines (<m> pipelines in 16 lines) and whenever free memory falls more than <step> MB below the lowest shown (free <a> MB less the floor <f> MB in 16 steps): at most 16 + 17 lines` | the set's start |
| `rd: pipelines: compiled ... batch ...; system: free 5059 MB (-2190 since the set began), file cache ..., GPU ..., the rest ... MB, swap used ... MB; each (s): ...` | the batches the rule picks (`rd_set_memory_clause_fn`; not in the activity file's copy) |
| `ico_pc: memory at the start-up set's end (<k> of <m> pipelines made; <c> clauses on its batch lines, <a> by count and <b> by a fall): ...` | the set's end (a total of 0 after one) |
| `ico_pc: memory over the start-up set: <k> pipelines made in <s> s; free memory <a> -> <b> MB: <x> MB lost a pipeline; the memory in use <+d> MB, what grew a pipeline (they sum to it): file cache <+x>, shared ..., the rest <+x> MB; swapped out <+x> MB a pipeline; this process <+x> MB a pipeline (resident); the most: <part>` | after it |
| `ico_pc: memory at the system's signal (onTrimMemory level <n>): ...` | each `onTrimMemory` |
| `window: memory in the block: ...; the system now: free ... MB (... since the set began), file cache ..., the rest ... MB, swap used ... MB` | the 10 s block line (Android) |

The batch lines' rule: `N = ceil(m / 16)` pipelines and `step = ceil((free at the set's start -
the floor) / 16)` MB, at least 1 (no floor: the free memory itself); any clause starts both counts
again, and a fall is measured from the lowest free memory a clause or the start line showed. So a
set carries at most 16 clauses by count and 17 by a fall (each lowers the lowest shown by more than
the step, and the set stops at the floor): with the start, rule, end and summary lines at most 37
lines for any set (38 with the sources' line, once a run) (the 684 pipelines of issue 215: every 43 pipelines and 334 MB). Nothing here
changes behaviour. Test: `mem_breakdown`.

### The start-up set's size

Issue 215: on Qualcomm's own driver the device's free memory falls with every pipeline made and
does not come back during the run, so a start-up set that runs to the free-memory floor leaves
play below it (both logged deaths of the issue 215 handheld came under the floor). The set is
sized by each start's own measurement ([PIPELINES.md](PIPELINES.md#sized-by-the-memory-fall),
`port/platform/set_size.h`); it never acts on how an earlier run ended, and where the free memory
does not fall with pipelines, or no floor applies (every system but Android), nothing changes.

| line | when |
| --- | --- |
| `ico_pc: pipelines: the start-up set's size by the device's memory fall: on (<m> pipelines; free <a> MB, the floor <f> MB; play's known need not made yet <n> MB: the VU pre-pass's record <r> MB and the mesh arena's first chunk <c> MB; made already, before the free figure was read: the record <R> MB, the chunk <C> MB): ...` or `...: off (<why>): the set as before` | the set's first ask |
| `ico_pc: pipelines: the start-up set's size at pipeline <p> of <m>: free <a> MB less the floor <f> MB less play's known need <n> MB leaves <h> MB; 2D <x> MB a pipeline (the median of <b> batches): its <k> left x <x> = <c> MB, stops now, the rest made on first use; VU not measured yet (0 of 3 batches), measured when reached; the rests kept <s> MB` | a family's first trusted figure, and each stop |
| `rd: pipelines: the start-up set sized by the device's memory fall: <k> made (2D <a>, VU <b>), <l> left to first use (...)` and `rd: pipelines: no start-up set (memory: the start-up set sized by the device's memory fall: <why>; <l> pipelines left to first use): ...` | the set's end, when a family was stopped |
| `window: pipelines: the start-up set stopped at pipeline <p> of <m> (...) (memory: ...); the next start makes the whole set again` | after it: no resume record |
| `ico_pc: memory at the first picture: free <a> MB of <t> MB (the floor <f> MB), the lowest since the start <l> MB; the start-up set sized by the memory fall: VU left to first use (14.0 MB a pipeline)` | the first picture, where the rule was on |
| `window: memory: ...; pipelines ... alive ...; <n> made on first use in the block (the longest <s> s), <r> in the run` | every 10 s block |

`last-run.txt`'s phase carries the decision's words after the phase's own (`...; start-up set
sized by the memory fall: 2D, VU left to first use (15.0, 24.0 MB a pipeline)`), from the decision
on. The attempt ledger's row `the start-up set sized by the memory fall` is `on` where the floor
applies (`measured at each start ...`), `not applicable here` elsewhere; its counts are the
pipelines left to first use and those made on first use after the stop.

The issue ledger, for this rule:

| Problem the logs showed | The question the next log answers | The line and field that answers it |
| --- | --- | --- |
| Retroid Pocket 6 (issue 215): every start's set drove free memory down to the floor (7249 -> 1911 MB at the release before), and both deaths came under it | Does play now start with memory to spare, and how low does it go? | `memory at the first picture: free <a> MB ..., the lowest since the start <l> MB` against the release before's 1911 MB at the floor; the block lines' `the device's least <n> MB available against the floor` |
| The same: what the set left out costs | How many pipelines play made on first use, and how long the longest took | the 10 s line's `<n> made on first use in the block (the longest <s> s)`, the ledger row's two counts, the lazy batches' lines |
| The issue 123 tablet: the set stopped at the floor in its VU half | Did the rule stop VU early and keep 2D (no fall there)? | the size lines' `2D no fall ... goes on` and `VU ... stops now` |

### How the previous run ended

At the window's start, before the start-up activity file and `last-run.txt` are read and
rewritten, one line says how the previous run ended (`port/platform/exit_info.h`, `exit_info.c`,
issue 215):

```
ico_pc: the previous run ended: <how> (<Android's part>); it was at <phase>, <s> s after its start at its last write; GPU resets <a> -> <b>; free memory <n> MB at its last write (lowest <l> MB, floor <f> MB)
```

`<how>` comes from the first of these that holds: our own end mark (`last-run.txt`'s end: `it
crashed (signal 11)`, `the graphics device was lost`, `the graphics driver stopped responding (a
GPU hang)`, `the watchdog ended it: <reason>`, `it was closed normally`; or the start-up activity
file's loss line); then Android's exit record whose pid is `last-run.txt`'s, in plain words
(`Android ended it because the device was low on memory`, `it crashed in native code`, `it stopped
responding (Android's "not responding")`, `it was closed from the recent apps list or with Force
stop (or updated, before Android 14)`, `it was killed (signal 9, SIGKILL); this device does not
report low-memory kills as such, so it may have been one`, ... one for each of
`ApplicationExitInfo`'s 17 reasons in the public SDK, 0-16; another number is printed as its number, `a reason not known to this program`); then the files alone (`without a word: no crash, device loss,
watchdog or clean end was written`, `without a word inside the start-up (its activity file was
left; no last-run.txt)`, or `unknown: no last-run.txt and no start-up activity file ...`).
`<Android's part>` is the matched record (`Android's record: REASON_LOW_MEMORY (3), status 9, in
front (importance 100), pss 210 MB, rss 260 MB, at 2026-11-16 20:50:15 UTC, "<description>"`), or
`no record from Android for pid <n>; <k> other new records`, or why there is none (`the system
keeps no exit record (Android 11 and later only)` off Android, `Android API <n> keeps no exit
record (...)` below Android 11, `Android's exit records not read: <why>`). The phase, seconds, GPU
resets and free memory are `last-run.txt`'s last write; without the file, the record's own summary
(`last_run.h`'s `setProcessStateSummary` text) or the activity file's note stands for the phase.
The line is at most `EXIT_INFO_VERDICT_MAX` (1000) bytes, each part in its own room, so it is never
cut in the log. Nothing changes behaviour by it.

On Android 11 (API 30) and later the records come from
`ActivityManager.getHistoricalProcessExitReasons` (`IcoActivity.exitRecords`, `ExitRecords.java`,
called through `port/platform/android/exit_info_android.c` with the limits as arguments): only the
records newer than the newest one an earlier start reported (`logs/exit-info-seen.txt`: its time
in ms and the newest trace's name), the newest 8 of them. A record with a trace (an ANR's text;
from Android 12 a native crash's tombstone, a protobuf) has at most 512 KB of it copied to
`logs/exit-trace-<time ms>.txt` (`.pb` for a tombstone), for the newest two records that have one;
only the newest two such files stay. Then:

- `ico_pc: Android's exit records: <n> new since the last read (<t> held in all, <o> older, <b>
  past the 8 kept, <m> lines not read); low-memory kills reported as such: yes | no (they show as
  signal 9) | not said` (`ActivityManager.isLowMemoryKillReportSupported`);
- `ico_pc: Android's exit record <i> of <n>: pid <p>, REASON_<NAME> (<code>): <words>; status <s>;
  importance <i> (<words>); pss <n> MB, rss <n> MB; at <UTC> (<ms> ms); trace <file or none>;
  description "<the system's text>"; summary "<the process's summary>"`, one a record;
- `logs/exit-info.txt`: the verdict, the system's line, the record lines and the newest trace
  kept, rewritten at every start. A report zip carries it and the newest trace, and `report.txt`
  carries the verdict after its memory line (`ico_report_set_exit_info`).

Off Android the system keeps no record: the line says so and uses the files alone
(`ICO_TEST_EXIT_INFO` feeds records to the tests, [TESTING.md](TESTING.md)). Test: `exit_info`,
`report`.

### The symbol lines

A crash or watchdog report names, after its raw lines, every offset in the program it printed (the
faulting address, the stack's return-address candidates, the thread table's `fn <module>+0x...`
and each last kernel call's caller), in the order printed, from the symbol file shipped with the
program:

```
ico_pc: symbols from ico_pc_x64.map (each offset above in ico_pc_x64.exe, in order):
ico_pc:   +0x1b35e4 _CopyVector+0x4 (vu0.c.obj)
ico_pc:   +0x9add a static function in diag_test.c.o, +0x11d into its code
```

The file is the link map beside the program (GNU ld's form, which llvm-mingw's lld also writes for
the Windows program; lld's own ELF form, with its VMA, LMA, Size and Align columns and local
functions named too, for the `linux-x64-clang` preset), its name the program's with `.exe` replaced
by `.map` (`x64/ico_pc_x64.map` on Windows, `ico_pc.map` on Linux; both packages ship it), opened
at `ico_diag_init` and read only when a report is written; on Android it is the APK's
`assets/libmain.syms.gz`, `llvm-nm -n` of the unstripped `libmain.so` gzip-compressed and stored
by `tools/package_android.sh`, whose range of the APK file the window opens at start with
`AAsset_openFileDescriptor64` (`port/platform/android/host_android.c`
(`ico_android_open_symbols`); `ico_diag_set_symbols_fd`). macOS and iOS ship no map in the bundle:
their reports say so and keep the raw offsets (`port/platform/diag_host.c` (`sym_open_default`,
`report_symbols`)).

The parser (`port/platform/symmap.c`) streams the file twice through static buffers, a line at a
time (lines cut at 4096 bytes): the first pass finds the anchor `ico_diag_init`, whose map address
minus its offset in the running module places the map against the program (a PE map's image base,
a PIE's zero and a shared library's table read alike); the second compares every offset (64 at
most) with every symbol line and input section line of the code's output sections (`.text`,
`.init`, `.fini`, `.plt`). A map names global functions only: a symbol counts only inside its own
object's input section, and an offset in an object with no global function before it is printed as
the object and the offset into its code (`a static function in rd_blur.c.o, +0x1c0 into its
code`); a `.text.<name>` section (`-ffunction-sections`) names its function; an nm table's `t`
lines name static functions too. The map is checked against this build: `ico_diag_log` must come
out at `+0x0`, else one line says the map is not this build's and the offsets stay as they are.
Nothing is allocated, so it runs inside the signal handler of a crash; the symbol lines come after
the raw ones, so a parser that fails loses nothing; a 3 MB map is a few milliseconds
(`port/platform/symmap.h`).

### The report file

On a crash, a watchdog stop, a lost graphics device and graphics that could not start, the port
writes one zip, `logs/report-<date>-<time>.zip` (a second in the same second
`report-<date>-<time>-2.zip`), the one file a player attaches; the boxes of those ends say "attach
the newest file in logs that starts with report-" and name it (`port/platform/diag_host.c`
(`report_file`, `report_box`, `fatal_text`); `port/platform/window_host.c` (`device_lost_quit`);
`port/platform/main_host.c` (`open_window`)). It holds:

| entry | what |
| --- | --- |
| `report.txt` | what ended the run and when, the `VERSION.txt` lines (beside the program or one folder up, then on Linux `../share/doc/ico-pc/` from the running program's own folder, where the AppImage keeps it; Android's from the app's assets; `port/platform/report.c` (`ico_report_set_paths`)), `achievements.toml`'s `stages` line (not the file), the disc played (`disc: SCPS-11003 (the Japanese disc)`, `ico_diag_set_disc`), the [summary line](#the-summary-line), the process's and the device's memory when the report is written (`memory: memory 191 MB (most 197 MB); device 320 MB available of 7632 MB`, `IcoReportInputs.memory`, `ico_diag_memory_text`; `(unknown on this system)` without `/proc`), the log's latest device, robust access, blend, VU index, fog, shader and window lines, the last 20 `ico_pc:`, `window:` and `rd:` lines before the report, the report block itself (the crash or watchdog report with its symbol lines), and the window's own part (`IcoReportInputs.extra`, `ico_diag_set_report_extra`: SDL's own log, the last 64 lines) |
| `ico-pc.log` | the log so far |
| `ico-pc-previous.log` | the run before's |
| `startup-activity.txt` | when there is one |
| `last-run.txt`, `last-run-previous.txt` | where this run is and where the run before was, when there are ([the last-run file](#the-last-run-file)) |
| `exit-info.txt`, `exit-trace-<ms>.txt` or `.pb` | how the run before ended and Android's exit records read at this start; the newest trace Android kept with a record ([how the previous run ended](#how-the-previous-run-ended)) |
| `config.toml`, `ico-pc.ini` | the settings |
| `loss-<stamp>.zip` | this run's [loss dump](RENDER.md#the-loss-dump), else the one the previous run left, deflated |
| `slow-<stamp>.zip` | this run's [slow-scene dumps](ANDROID_DIAGNOSTICS.md#issue-179s-lines-what-each-decides), up to three (the newest three are kept), each stored as it is |

`port/platform/report.c` writes it without allocation: raw file calls (`open`/`read`/`write`/`lseek`,
the wide Win32 calls through a static buffer), a zip writer of its own (UTF-8 names, the sizes
patched into each local header) and miniz's `tdefl` deflating into a static compressor; the local
time from an offset noted at start, so no time zone lookup runs in a signal handler. The loss
dump's writer (`dump_zip.c`) is not used: miniz's zip writer allocates, which a crash inside
`malloc` would deadlock. `ico_report_prune` keeps the newest five: it sorts the `report-YYYYMMDD-HHMMSS[-k].zip` names
explicitly by stamp, then by `k` (the plain name is `k = 0`, so a same-second `-1` is newer; the
folder's order and byte order are not age), and removes the oldest. It lists the folder and so runs
only on the normal paths (at start, after a lost device's or a failed start's report)
(`port/platform/report.h`). The line `ico_pc: report: report-<stamp>.zip (N.N MB) in the logs folder (...)`
names it and its size.

### The summary line

Once a start, after the device line, the self-tests and the window line, the window logs one line
with what a report usually asks:

```
ico_pc: summary: version vX.Y.Z (commit 1a2b3c4d); Windows x64; GPU NVIDIA GeForce GTX 1060 (device 10de:1c03; driver NVIDIA 566.36, version 566.36.0.0); Vulkan; Enhanced preset, resolution ..., aspect ..., framerate ...; window mode ...; blend fixed-function (self-test: fixed-function ok 1); profile immediate-mode GPU (NVIDIA, Vulkan): fog local on, DATE stencil, depth stores planned, blur fold on, glow fold on, local read yes, uint views work and scene, alpha plan on, fx plan on, fx local on, VU planes whole scales only, shadow count one draw, shadow edges: bilinear, desktop local read kept (probe 0.412 vs 0.530 ms); index values dense (marks in the stream); fog local (self-test: ...); VU shaders one set; VU pre-pass on (self-test: ok; record stores); pipelines the start-up set; index values up to 4294967295; robust access off (the default; gpu_robust = true turns it on); controller Xbox Series X Controller (DirectInput scan on); disc SCES-50760 (the European disc); user profile
```

(`port/platform/window_host.c` (`log_summary`); `port/render/rd_core.c` (`rd_summary_text`, from
the self-tests' results); `port/render/rd_profile.c` (`rd__profile_text`); `port/rhi/rhi.h`
(`RhiLimits.driverText`, `robustText`)). The blend is the self-test's
([RENDER.md](RENDER.md#destination-reads-and-blend-paths)): `fixed-function` by default, an exact
mode with `; kept by [dev] blend` where the player opted in; the profile is the device class and
its flags as in force, `([dev])` after one a `[dev]` key set, `(self-test)` after one a self-test
turned off and `(probe)` after one the desktop's local-read probe turned off, and on desktop Vulkan
with local read the probe's verdict last ([RENDER.md](RENDER.md#the-device-profile),
[WORK_BUFFERS.md](WORK_BUFFERS.md#the-desktops-local-read-and-its-probe)). The controller
is what SDL has connected then ("none yet" when the pad arrives later; its own `input: gamepad`
line follows), on Windows with whether SDL's DirectInput scan is on (`[dev] joystick_directinput`);
the VU shaders are `one set, fallback plain (<reason>)` on a device whose fallback takes the
plain program entries (`(Adreno 740, issue 107)` for the device table's row,
`RhiLimits.vuFallbackPlain`; `(Turnip driver, issue 163)` under Turnip; [RENDER.md](PIPELINES.md#the-fallback-entries)), and the pipelines `the careful start-up set`
under the careful policy, `the careful start-up set, stopped at its budget (the rest on first use)`
when its budget stopped it, and `on first use (no start-up set)` where the set is skipped
(`port/render/rd_core.c` (`rd_summary_text`); [RENDER.md](PIPELINES.md#the-start-up-set)); the disc is the one `select_disc` named (`ico_diag_set_disc`); on
Android the disc is identified after the window opens, so the line first says `disc not mounted
yet` and is logged again with the disc in its place once it is known, which the report file's copy
keeps.


`ico_diag_set_summary` flushes the C library's held lines first, so on Windows the line follows the
device and window lines in the file, and keeps the text for the report file. The line is longer
than a log line: up to 2048 bytes (`SUMMARY_MAX_BYTES`), written whole to the file and the report
file (whose log reader takes lines of 2080 bytes, `port/platform/report.c` (`LINE_BYTES`)) and on
Android to logcat in pieces of 1023 bytes (`port/platform/diag_host.c` (`summary_log`,
`raw_write`)). The 10 s block's
`window: memory:` line has the graphics memory in use and its peak where the backend counts it
(Vulkan), the process's, and at its end the RHI's pipeline pool on both backends: `; pipelines
<live> alive (most <peak> of the pool's <size>), <f> creations failed (<p> for a full pool)`
(`RhiStats` `pipelinesLive`, `pipelinesPeak`, `pipelinePoolSize`, `pipelineFailures` and
`pipelinePoolFull`; [RHI.md](RHI.md#the-pipeline-pool)), so a log shows a pool that ran out
(`port/platform/window_host.c` (`perf_log`)).

A Vulkan picture with every other column black and bright dots on a Radeon RX 7000 card with Mesa
25 RADV (issues 146 and 154: the driver read a leftover dual-source blend equation on draws without
blending, [RHI.md](RHI.md#dynamic-state)) does not show in the summary or in an F12 dump, which is
saved before the card draws it; the backend now sends the equation on every change, and a driver
that still drops those columns fails the blend self-test at its after cell (`rd: blend: self-test:
fixed-function fail <n> at after cell, odd column (texel <x>,<y>): ...`) and takes the two-pass
blend, which the summary then shows as `fixed-function fail <n> -> two-pass ok <m>`
([RENDER.md](RENDER.md#destination-reads-and-blend-paths)).

### Rate-limited lines

Every line that may repeat (a per-frame or per-event condition) goes through one helper,
`ico_log_limited(key, fmt, ...)` (`port/platform/diag_host.h`): the first 8 of a key are written,
then every 100th, the 8th saying `(8 of these so far; from here every 100th is written)` and a later
one `(number N of these)`; `ico_log_limited_n(key, first, every, fmt, ...)` gives a key its own
bounds (SDL's error lines: 64 and 1000), its line saying them the same way; the hot path is one atomic increment and a compare, the arguments
evaluated and the line formatted only when it is written. At the end of a run, normal or fatal, one
line counts every key: `ico_pc: limited lines (key, count, written): rd: interp snap 412 (11
written); ...` (`port/platform/main_host.c` (`summary`, `fatal_summary`)).

## The evenness line

When the motion does not look as smooth as the presented rate says, read the 10 s line the window
prints after the CPU phases' line (`port/render/rd_even.c`, `rd_even.h`; `port/platform/window_host.c`
(`perf_log`)):

```
window: evenness in 10 s: P pictures; the GPU's ends apart (E with both ends exact, B with a bound at one, U without a GPU end) by refreshes of R ms: under half a refresh N0, 1 refresh N1, 2 refreshes N2, 3 refreshes N3, 4 or more N4; the content's step over them in ticks (mean, deviation): under half a refresh M0 (D0), 1 refresh M1 (D1), 2 refreshes M2 (D2), 3 refreshes M3 (D3), 4 or more M4 (D4); every step (S): mean M (deviation D), the largest L, back K times (the most X), C chain restarts; the alpha clamped at 0 Z times (the most A below), at 0.999 T times (the most H above); the present clock: N samples, resets F forward and K back, H held instead of going back within a frame pair (the most X ms); the alpha's sample to the GPU's end over N pictures: 10th A, median B, 90th C ms, the hand-over to it 10th A, median B, 90th C ms (the drain before the hand-over D ms on average over N); re-based to the hand-over: N pictures (the shift 10th A, median B, 90th C ms; H held at the frame's last value), the content's moment to the hand-over 10th A, median B, 90th C ms (at the sample 10th A, median B, 90th C); the content drawn against the hand-overs over P pairs: R repeats (the same moment as the picture before) in U runs, the longest L; the step over the real gap from A to B, outside 0.5-2.0 in N; the GPU's predicted end: P pictures placed by it (O with one a tick, N before its measurements, R restarts), the shift 10th A, median B, 90th C ms (the queue's wait Q ms on average), K at the bound (the last W ms, from the hand-over to the measured end 10th A, 90th B ms), H held to 0.5-2.0 times the hand-overs' gap; predicted against the measured end over M pictures: mean E ms (deviation D)
```

`every step` also says, when some steps are over gaps the line cannot put on the grid (a bound or
no GPU end at one side), `, U of them over gaps not on the grid: mean M (deviation D)`.

- **The pictures** are the presents whose GPU record was collected in the block (each frame slot's
  record, two pictures after it was made). A picture's content is its frame number plus its alpha,
  in ticks (one tick a game frame, 33.4 ms at NTSC's 30 frames a second).
- **The GPU's ends apart.** A picture's GPU end on the CPU's clock is the end of the frame slot's
  fence wait when that wait blocked 0.05 ms or more (exact), its start when it blocked less (a
  bound), and none when the work had finished before the wait (the GPU ahead of the CPU: usual on a
  desktop GPU with time to spare). Only gaps with both ends exact are put on the refresh grid, by
  their quarter-of-a-ms bin's middle over the display's refresh R rounded to a whole number. On a
  120 Hz panel at 60 to 73 pictures a second most gaps are 1 or 2 refreshes; without a refresh
  (the display reports none) the line says `the refresh not known, not put on its grid`. A GPU end
  is not the moment the picture reached the glass: the compositor shows it at a later refresh, so
  the grid here is the GPU's cadence, not the display's.
- **The content's step over them.** The mean step for the gaps of each class. Equal steps over
  1-refresh and 2-refresh gaps mean the 1-refresh pictures move twice as fast as the 2-refresh ones
  on a panel that holds each picture until its next refresh; steps in proportion (the 2-refresh
  mean twice the 1-refresh one) mean the content follows the pictures' own spacing.
- **Every step.** All steps between consecutive presents (any GPU end): the mean, its deviation,
  the largest, and the steps that went back (a later picture showing an earlier moment) with the
  largest. A frame number that went back or moved on by more than 8 restarts the chain (a load, a
  new run) and is not a step.
- **The alpha clamped** at 0 (the present clock still before the newer frame's close: the picture
  shows the frame before exactly, and the content jumps on by the amount at the next one) and at
  0.999 (the clock past the newer frame), with the largest amount in ticks.
- **The present clock** (the timer path only; under the vsync lock the clock is not used and its
  samples are 0): the presents it timed, its resets that moved it forward (or kept it) and those
  that would have moved it back, and the presents where it was held at its last value within a
  frame pair instead of going back, with the largest amount it would have gone back. See
  [DISPLAY.md](DISPLAY.md) "The alpha".
- **The alpha's sample to the GPU's end**: for presents handed over to the render thread with an
  exact GPU end, the time from the drain's start (the alpha is taken just before it) to the GPU's
  end, by the 1 ms bin's middle; the drain is the wait for the picture before, after the alpha and
  before the hand-over (the game thread's `slot waits` split, fresh and further). A wide 10th to
  90th spread is latency that varies picture to picture: content sampled at even times reaches the
  GPU's end at uneven ones. The hand-over to the GPU's end is the same without the drain: a spread
  much narrower than the sample's says the drain is what varies, and that an alpha taken at the
  hand-over would reach the GPU's end at steadier latencies.
- **The steps over gaps not on the grid.** A picture has no GPU end when its frame slot's work had
  finished before the CPU waited for it: the GPU was idle before the next picture, which happens
  after a picture between frames was held (the step rule) or the presents paused. The steps over
  those gaps are the large ones (about twice the classed steps on the owner's phone), and they make
  `every step`'s deviation far larger than the classes'. They are not classed because their gap
  is not measured; read their mean apart from the classed ones.
- **Re-based to the hand-over** (pictures as made only: vsync off, uncapped, the render thread on;
  [DISPLAY.md](DISPLAY.md) "The alpha"): the presents whose alpha was taken at the hand-over less
  the drain's running mean, the shift's spread, the presents held at the frame's last value, and
  the distance from the content's moment to the hand-over: with the re-base, the drain less the
  shift (about the mean drain, a ms or so of spread); `at the sample`, the drain less the present
  clock's lead over the ask, which is the clock's own error against real time (up to 7/16 of a tick
  each way).
- **The content drawn against the hand-overs** (every handed-over present on the timer path): the
  pairs of consecutive presents more than a ms apart, the repeats (a present whose content moved
  under a microsecond: the same moment as the picture before, which the eye sees as a stutter),
  their runs and the longest, and the least and largest ratio of the content's step to the
  hand-overs' interval, with the count outside 0.5 to 2. A ratio of 1 is content following real
  time picture to picture.
  The `alpha clamped` counts are the clock's value's; the `interp:` alpha line counts the alpha
  drawn.
- **The GPU's predicted end** (pictures as made only; the clause is left out when no picture was
  handed over as made in the block; `port/render/rd_pace_gpu.c`, [DISPLAY.md](DISPLAY.md) "The
  alpha"): P the pictures whose moment the GPU queue's model placed (moved by its predicted queue
  wait less the wait's running mean), O those it left at the hand-over because no picture of the
  last 16 was a further one of its frame (one picture a tick), N those before its windows were full
  (the first pictures of a run, after a restart, and every picture on a backend without the slot
  clock: OpenGL, Direct3D 11, timestamps off), R its restarts (a frame number back or on by more
  than 8, hand-overs over a second apart). The shift's spread over P (by the half-ms bin's middle)
  and the queue's mean wait; K the pictures whose shift was held at the bound, W the bound at the
  block's last placed picture and the 10th and 90th of the measured hand-over to end times it came
  from (the larger of 90th less mean and mean less 10th); H the pictures whose step was held to 0.5
  to 2 times the hand-overs' gap. `predicted against the measured end`: for each picture whose end
  was stamped, its end as predicted at its hand-over less the stamped end (negative: the GPU ended
  later than predicted), the mean and the deviation over M, `no exact end` with none; a wide
  deviation or a mean far from 0 says the prediction is off and the shifts with it. `(N
  measurements dropped)` follows when the poster's thread fell 32 records behind. On the owner's
  120 Hz phone in the cage P should be nearly every picture, the shift a few ms each way, and
  `every step`'s deviation well under the .164-.187 the hand-over's moment gave there; at one
  picture a tick O is every picture and nothing else changes. The model is off by default (its
  moment computed and reported, not drawn: [DISPLAY.md](DISPLAY.md) "The alpha"), so P counts the
  pictures it would have placed.

No clock is read for the line: the stamps are the ones the drain, the slot wait and the present
clock already take. The render thread adds about a dozen relaxed atomic additions a picture, the
present clock one to three a present (a compare-and-swap only when a maximum grows), and the window
formats one line every 10 s.

## The on-glass line

Where the display's own times of past presents come back (Vulkan with `VK_GOOGLE_display_timing`,
read only: `rhi_present_glass`, `port/rhi/vk/vk_glass.c`; on Android alone, and only while pictures are presented as made: vsync off, Frame rate Uncapped, the render thread on), the window prints after the evenness line
(`port/render/rd_glass.c`, `rd_glass.h`). Elsewhere it says once `window: on the glass: not on this platform (the display's own times are read on Android alone, for the screen's rule)`; in capped and vsync modes on Android no picture carries an id, so the line is absent there:

```
window: on the glass in 10 s: P pictures with a display id, S shown with the display's time, U presented with no time back (replaced before shown, or not reported), N pairs; the display's gaps by refreshes of R ms: under half N0, 1 N1, 2 N2, 3 N3, 4 or more N4; the content's speed on the glass (its step over the display's gap, 1.00 real time), drawn (RULE): mean M (deviation D), above 1.25 A %, above 1.5 B %, below 0.75 C %; by the display's gap: 1 refresh M (D), 2 refreshes M (D), ...; against the hand-overs M (D) over K, against the GPU's ends M (D) over K; by the hand-over (drawn|not drawn): ...; by the GPU's predicted end (...): ...; by the own timer (...): ...; by the screen's predicted refresh (...): ...; the display's own figures over N shown: present margin M ms on average [over K], [present margin over a second either way W (S ms on average: not a margin, no desired time was asked),] earliest present half a refresh or more before the time shown K (by E ms on average); the screen's predicted refresh: N pictures placed by it, M drawn as otherwise (no display time back K, no predicted GPU end K, too few shown pictures yet K, the display's times stale K, the display's times off one refresh grid K, the latch not predictable K, a kept frame (a fade or a load, its GPU work not modelled) K), R restarts; held at the frame's close A, at the newest frame B, at the moment before C; D on the refresh of the picture before (replaced before shown); G by the exact ends' measured errors[; the display's refresh changed K times (the grid and the latch fitted again)]; the refresh predicted against the one shown over S pictures: H on it (P %), L one late, E one early, F further; the display's grid: a refresh of R ms measured (Q reported), O of N times off it; the latch: shown at the first refresh at or after the predicted GPU end + X ms (H of N agree, the prediction's spread W ms), after the exact GPU end + Y ms (H of N); the lag L ms; the even cadence: N refreshes a picture (the last; F a second), C changes; A pictures aimed (H held before the hand-over, M ms on average; B before the game's step), L late for their refresh at the hand-over; not aimed: REASON K, ...; shown off their refresh O of S (L later, E earlier)[, K never shown]
```

- **The pairs** are consecutive shown pictures in the presents' order (a picture's record joined
  with its display time by its id). U counts the ids between two shown pictures that never got a
  time: in mailbox mode a picture replaced before a refresh, or a time the driver did not keep. A
  frame number back or on by more than 8, or two pictures over a second apart, is not a step.
- **The speed**: the content's step from the shown picture before (frame plus alpha, in ms of game
  time) over the time between their display times. 1.00 is real time; a step over 1.25 or 1.5 is
  the content moving faster than real time for that picture (what reads as "sped up"), under 0.75
  slower. The deviation is the unevenness the eye sees. By the display's gap in refreshes (the
  driver's refresh) the classes show whether the long steps are the content's or the display's.
  The step is the frames between plus the alphas' difference, in the newer picture's tick (the
  measured gap between frame closes, which moves picture to picture). An earlier form took each picture's
  content as its frame number plus alpha times its own tick, so a tick 0.1 ms longer at frame
  6000 read as a 600 ms step: issue 123's log of 2026-11-14 showed deviations of 84 to 1040 and negative
  means in exactly its stall blocks, where the tick moved most.
- **The rules**: `drawn` names the rule the pictures were drawn by (the hand-over, the GPU's
  predicted end, the own timer, the screen's predicted refresh, or the present clock in paced
  modes). By default with pictures as made on Android it is the screen's predicted refresh where
  the rule placed the picture, otherwise the own timer; on every other build the hand-over (the
  screen's rule and the own timer reported as `not drawn`) ([DISPLAY.md](DISPLAY.md#interpolation-between-ticks)). With pictures as made the
  three rules are computed at every picture (`port/render/rd_pace_rules.c`) and each gets the same
  measures (its own alpha, clamped as drawn, without the frame's hold), so one run compares them on
  the glass; the drawn one's figures equal its `drawn` figures.
- **Against the hand-overs and the GPU's ends**: the same step over the hand-overs' interval and over
  the GPU's ends' (both exact), so the three clocks are side by side: a rule even against the GPU's
  ends and uneven on the glass says the display does not follow the GPU's ends.
- **The screen's predicted refresh** (`port/render/rd_pace_screen.c`; [DISPLAY.md](DISPLAY.md#interpolation-between-ticks)) is the fourth rule computed: its measure is the same as the others', and its clause says how many pictures it placed and why the rest were drawn as otherwise (the reasons only where they occurred), its holds at the tick's edges and at the moment before, the pictures predicted on the refresh of the picture before (mailbox replaces the earlier), its score (each shown picture's refresh against the one predicted at its hand-over, in refreshes, by the reported refresh), the display's grid as fitted (the period measured from the shown times against the one reported, the times off it), the latch (the offset from the predicted GPU end that the most of the last 64 shown pictures agree with, how many, and the prediction's spread from the misses; the same fit from the exact GPU ends, the compositor's own lead where both clocks are one) and the lag from the display's time to the content (it holds the clocks' offset, so only its changes mean anything). The fit's figures are the last placement's. Where no picture carries an id the line is `window: on the glass in 10 s: no picture with a display id` followed by the clause, printed only where pictures were made as fast as the GPU allows.
- **The display's own figures**: `presentMargin` and `earliestPresentTime` of the same shown pictures as the driver reports them (`VkPastPresentationTimingGOOGLE`), the margin's mean and the pictures whose earliest time was half a refresh or more before the time shown; printed for comparison, used by nothing. The margin is an unsigned field read signed, averaged only within a second either way (`RD_GLASS_MARGIN_MAX_NS`); the rest are counted apart with their own mean: with no desired time asked the owner's phone gives values about 20 s below 0, which summed unsigned printed -7373559260669.68 ms.
- **Kept frames** (a fade, a load, a menu drawn over the picture before) draw no alpha: no rule's measure takes them, and the screen's rule neither samples nor scores them (`a kept frame ...` among its reasons).
- **The even cadence** (`port/render/rd_pace_even.c`; [DISPLAY.md](DISPLAY.md#interpolation-between-ticks)), off by default, so only where it ran (`ICO_RD_PACE_EVEN=1`; off, the line has no clause for it): N and the pictures a second it gives on the measured refresh (the last), how often N changed, the pictures aimed at a refresh, how many the pacer held before their hand-over and for how long on average, how many hand-overs it made before the game's step rather than after it, how many could not make their refresh at the hand-over (aimed at a later one), the reasons the rest were not aimed (`the screen's grid or latch not fitted`, `the GPU's time not measured yet`, `a kept frame`, `handed over without a plan`, `a picture longer than a game frame`), and of the aimed pictures shown with the display's time, those shown on another refresh (with the desired present time `earlier` should be 0), and those never shown (passed by a later aimed picture: 4 of the last 32 turn the cadence off for the run, the line `rd: an even number of refreshes a picture: off for this run (its self-check: ...)`, as does a 3 ms rise in the acquire and present call against presents without a desired time; the reason `turned off by its self-check` then counts the pictures). With it the drawn measure is the screen's for the aimed pictures: its deviation is what the even cadence leaves.

No clock is read for it; the display's times are the driver's (its own clock: only differences are
used). Nothing is printed where no picture carried an id (another backend, the extension not
offered) unless the screen's rule ran (above).

## Issue 179's judging lines

The Mali-G52 reporter's log (a PAL disc at Frame rate Original, 25 pictures a second at best)
showed several 10 s lines judging against the wrong thing. What each now judges against, and the
lines added beside them for issues 179 and 123:

- **Slow pictures and slow steps** (`port/platform/slow_judge.c`, `.h`; `window_present.c`
  `window_slow_replay_ms`, `window_slow_step_ms`): a replay is a slow one (its `window: slow
  replay` line, the 10 s line's `N over their interval`) when it took longer than the larger of
  the display's refresh and the picture interval in force: Frame rate Original's one picture a
  game frame (the frame's steps times the step period, the last known through a load), a numeric
  cap's gap between presents, or one refresh (Uncapped, the vsync lock, a cap above the refresh).
  The slow-step bound (`[dev] slow_step_ms`, 8 by default: half a 60 Hz refresh) is scaled by the
  same interval over the refresh (19.2 ms at 25 pictures a second on 60 Hz). The line
  `window: slow pictures judged against 40.0 ms (the larger of the display's refresh 16.7 ms and
  the picture interval in force 40.0 ms), slow steps against 19.2 ms ([dev] slow_step_ms 8.0 scaled
  by 40.0 / 16.7) (change 1 of the run)` comes at each change (the first 8, then every 100th). A
  slow step's CPU part says where the rest went: `(CPU 3.2 ms of it; 7.8 ms off the CPU: the thread
  waited or was not scheduled)` or `(...: the thread's own work)`.
- **The worst second** (`port/platform/clock_trap.c`): each whole second with a picture is judged
  by its presented pictures against the rate in force (the same interval: 25 a second there), so a
  25 fps disc's dips (12-13 pictures a second, which the 20-frame rule left as `none judged`) are
  judged: `worst second of the block: 12 pictures for 12 game frames (1.00 a tick; 48 % of the
  25.0 a second in force; 9 whole seconds judged), from 4.0 s into the block; ...`. Where the rate
  is not known, the 20-frame rule as before. The pins' one-a-tick rule is unchanged.
- **The game's rate** in the performance lines (`the game 25.0 of 25.0`): the frames over the span
  from the frame close before the block to its last (`slow_judge_game_fps`), not over the block's
  wall time, whose ends fall between frames (`25.1 of 25.0`).
- **The vertex fit** (`port/render/rd_perf_rank.c` `rd_perf_rank_fit_spread`): the line's base
  is the fit carried from the mean invocations to none; its standard error,
  `sigma sqrt(1/n + mean(x)^2 / Sxx)`, must be under the pictures' mean work, else `the GPU's work
  against the vertex invocations: not enough spread (62.7k to 62.7k invocations a picture over 190
  pictures without keep frames: the line's base would be off by E ms, against W ms of work a
  picture)` and no line (the log had `-9224.2 ms + 147.916 ms a thousand`).
- **The record path's wording**: the Vulkan backend's `no vertexPipelineStoresAndAtomics: the VU
  record is not stored by the vertex shaders (the renderer's VU record line says how it is
  stored)`; the renderer's `rd: VU pre-pass: the record holds ... (stored by compute dispatches)`
  says which (the backend's line said the render targets where compute was then used).
- **What a stall coincided with** (`port/platform/stall_coincide.c`; `window_stall.c`): every
  timed record's pieces (each list, each effect, the VU pre-pass, the stretch before list 0, the
  present) go into the block's medians, and a stall's line adds `; its N timed pieces against
  their block medians: list 0 x6.0, the others x1.1 (their median; 1 of 4 at x2 or more)` or
  `every piece grew, x3.0 to x3.5`, `; the GPU idle 0.2 ms before its work` (or `its work begun
  1.5 ms before the picture before ended`) and `; 13000 VU triangles against the block's median
  12000 (x1.08)`. Two classes join the tally: `one piece grew` (two or more pieces, the largest at
  twice its median or more, the others' median under twice) and `the GPU slow as a whole` (two or
  more pieces, every one at twice its median or more).
- **Pass timing's cost** (`rd_perf_rank_stamp_cost_text`): each picture timed pass by pass against
  the mean of its neighbours in the block without pass timing: `...; against the pictures beside
  each without pass timing: +10.0 ms (the median of 5 pairs)` (the two sets' medians compared
  different pictures).
- **Bound by waits elsewhere** (`rd_perf_rank_verdict`): the verdict's bracket splits the GPU's
  work into the pictures drawn anew and the keep frames (`the GPU's work 34.1 ms: the pictures
  drawn anew 49.0 ms, the keep frames 24.0 ms`; the mean hid the drawn pictures behind the held
  ones) and adds `the GPU idle 10.0 ms a picture before its work` (the record's gap after the
  picture before); the GPU's idle time is a candidate of its own (`the GPU idle before a picture's
  work 10.0 ms measured a picture (idle before 104 of 208)`).
- **The automatic slow-scene captures** (`port/platform/window_scene.c`
  `window_slow_scene_due`): the last of the three is kept for the first slow block of play (the
  play layout dominant: the player in control), at any work over the bound; cutscene blocks take
  at most two until a play block's capture is made. Its line reads `in play (the dump kept for
  play): frame N ...`. The total and each capture's size are unchanged. The capture's copy still
  waits for the render thread's work in flight: the copy reads the texture and target records the
  render thread updates as it replays, and no hand-over that would let it copy at the end of its
  own picture has been shown safe.
- **A list's draws** (`port/render/rd_list_draws.c`, `rd_list_draws_live.c`): every pipeline
  look-up in a game list (a draw or a merged run of draws) counted by its list, fragment entry and
  the DATE mode its key carries; when the block's most expensive list (list 0 with the stretch
  before it on a tile-based GPU) is not list 0, `window: list 3's draws by fragment entry and DATE
  mode (the block's most expensive list, 23.1 ms a picture; pipeline look-ups a picture over 161
  replays, a look-up a draw or a merged run of draws): vu_ps no DATE in the key 62.0, sprite_ps
  DATE 0 by the stencil 21.0, ...; 3 more kinds 4.0`. `no DATE in the key` is a draw without DATE
  or one whose DATE reads the snapshot in its fragment entry (the key does not tell those apart).
- **An unmatched draw's texture scroll** (`port/render/rd_inames.c`): an unmatched name in the
  `rd: interp names:` line whose draws' VU block scrolls their texture (qw 2's x or y not 0; such a
  draw keeps its tick's scroll) carries `[scroll S]` (S of its count).
- **The held glow's camera** (`port/render/rd_glow_offset.c`; `rd_glow_hold.c`): the glow's
  250-frame line ends with `; the held glow's camera against the present's (the frame centre's
  direction): median 1.2, largest 4.5 GS pixels over 44 held presents`: the direction the tick's
  first present put at the frame centre, projected through each later present that pasted that
  glow (the pan's share of the slide; a near wall slides more by the eye's move).

### The issue ledger, continued

| Problem the logs showed | The question the next log answers | The line and field that answers it |
| --- | --- | --- |
| Mali-G52, 25 a second: every picture of play a `slow replay` against 16.7 ms | Which pictures are slow for the rate in force? | `window: slow pictures judged against ...` and the `slow replay` lines under it; the 10 s line's `N over their interval` |
| Mali-G52: the worst second `none judged` in the dips | Which second was worst against the rate in force? | `worst second of the block: ... (...; P % of the R a second in force; N whole seconds judged)` |
| Mali-G52: `-9224.2 ms + 147.916 ms a thousand` | Does the block's vertex work explain its GPU time? | The rates line's vertex fit, or `not enough spread (...: the line's base would be off by E ms, against W ms of work a picture)` |
| Mali-G52: every stall `none of these` | Did one piece grow, or the whole GPU, and was the GPU idle or the picture larger? | Each stall's `its N timed pieces against their block medians: ...`, `the GPU idle ...`, `... VU triangles against the block's median ...`; the tally's `one piece grew N, the GPU slow as a whole N` |
| Mali-G52: `+36.1` and `-21.1 ms` for pass timing's cost | What do the per-pass stamps cost? | `pass timing's cost: ...; against the pictures beside each without pass timing: +C ms (the median of N pairs)` |
| Mali-G52: keep frame blocks `bound by waits elsewhere` | Is it the drawn pictures' GPU work or the GPU waiting? | The ranked bracket's `the pictures drawn anew D ms, the keep frames K ms; the GPU idle I ms a picture before its work` and the candidate `the GPU idle before a picture's work` |
| Mali-G52: three automatic captures spent before play | What does a slow block of play hold? | `window: slow scene: ... in play (the dump kept for play): frame N ...` and its zip |
| Mali-G52: list 3 23-32 ms in the deja scene | Which draws make up the most expensive list? | `window: list L's draws by fragment entry and DATE mode (...)` |
| Adreno 740 tablet: the right wall's texture shifts | Does an unmatched part scroll, and how far is the held glow's camera from the picture's? | `rd: interp names:`'s unmatched names with `[scroll S]`; the glow line's `the held glow's camera against the present's (...): median M, largest X GS pixels over N held presents` |

## Issue 123's right wall: the camera rule on the plain set

The issue ledger's rows for it (after [The issue ledger, continued](#the-issue-ledger-continued))
and the attempt ledger's row ([VU1_PROGRAMS.md](VU1_PROGRAMS.md#the-camera-rule),
`port/render/rd_plain_camera.c`; its row in
[ANDROID_DIAGNOSTICS_LEDGERS.md](ANDROID_DIAGNOSTICS_LEDGERS.md#the-attempt-ledger)).

| Problem the logs showed | The question the next log answers | The line and field that answers it |
| --- | --- | --- |
| Adreno 740 on Qualcomm's driver at 3:2 (issue 123): a wall on the right of the main gate shifting or opening in a recording; the device's plain set kept the PS2's drops on every 3D draw (`rd: region rule: the camera decides (...); off on this device's plain set (issue 163): the PS2's drops`) | Did the camera rule hold the walls and floors there this run, and did the skinned draws keep their drops? | The start's `rd: region rule: the camera decides for static meshes on this device's plain set; skinned draws keep the PS2's drops (issue 163); [dev] region_rule = ps2 keeps the PS2's drops on every draw`; the 10 s line's `plain set (issue 163): S static and grid draws a replay by the camera rule, K skinned draws a replay keep the PS2's drops` (S above 0 in play: the path ran) beside `region rule: the camera's: N VU draws a replay` (N counts the particles too) |
| The same wall, should it stay | Is the wall still drawn by the camera rule while it shows, and is it the region rule at all? | The same two lines in the block of the recording, with the attempt ledger's row below; a wall that still shifts with S above 0 is not a region drop (the other candidates of the diagnosis: the held glow's camera against the present's and an unmatched part's texture scroll, the row above) |
| Issue 163's sliver (Turnip, a skinned draw's vertex thrown off) | Did the change bring it back? | The sliver came from a skinned draw's bones, which a static draw does not read; the skinned draws keep the drop: `K skinned draws a replay keep the PS2's drops` above 0, and the skin check's lines as before ([VU_SKIN_CHECK.md](VU_SKIN_CHECK.md)) |

The attempt ledger's row: `the camera rule for static meshes on the plain set on (static meshes and
grids by the camera, skinned draws keep the PS2's drops (issue 163))` on a device that keeps the
plain set under the camera rule, `not applicable here ([dev] region_rule = ps2: the PS2's drops on
every draw)` under the PS2's rule there, `not applicable here (this device draws with the extended
set or by choice with the plain one: the camera decides on every draw)` elsewhere (set at each
device start by `rd_pipeline.c` `rd__vu_policy_init`, `ico_attempt_decide_plain_camera`); its
counts in each block, `N mesh and grid draws by the camera (K skinned draws kept)`, are the draws
of the replays (`rd_plain_camera.c` `rd__plain_camera_note`, from `rd_replay_vu.c` `doVuDraw`).
The GPU decides each triangle, so the lines count draws; the triangles a dump's frame has dropped
or drawn per draw are `rd_replay_tool --region-stats`' on that frame (its oracle holds the plain
set's rule: no card test, no tick re-test, the skinned draws' drops kept).
