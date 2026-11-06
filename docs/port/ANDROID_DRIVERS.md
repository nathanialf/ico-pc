# Android graphics driver packages

Moved out of [PLATFORM.md](PLATFORM.md) (the file's size gate). The port's Android build can load a Vulkan driver package the player adds (a Turnip build for Adreno), the picker, the drivers folder, the driver page and the loss fallback.


The Android build can load a community Vulkan driver for Adreno GPUs (Turnip and others) through
libadrenotools instead of the phone's. A package is a flat zip of `meta.json`, the driver
library it names and that library's dependencies; installed packages live in folders under the
app's internal `drivers/` folder, the only place the system lets the app load a library from
(`port/platform/gpu_driver.h`). A trial marker names the driver a start is trying: written
before the driver opens, deleted after 300 presents or at a normal end, so a marker still there
at the next start means that start died inside the driver, which is then recorded as failed and
replaced by the phone's own driver with a message box
(`port/platform/android/gpu_driver_android.h` (`ico_gpu_driver_android_start`)). The package
handling is pure C and tested on every platform (`port/platform/test/gpu_driver_test.c`).

Add a driver ends in one of eight answers (`UI_GPU_INSTALL_*`, `port/ui/settings.h`), each its own
popup: `ADDED`, `BAD` (the zip was read and is not a package), `PAGES` (a library of the package has
PT_LOAD segments aligned to less than the phone's page size on a phone with 16 KB pages: the popup holds the
host's own English sentence, `UiGpuDriverHost.installWhy`), `NOSPACE`, `CANCELLED` (a popup that names
the drivers folder),
`NO_PICKER` (the picker could not be started for a reason other than a missing app, it answered
without a file address, or there is no drivers root), `NO_PICKER_APP` (no installed app takes
either picker intent, below) and `UNREADABLE` (the file would not open, the copy failed with space left,
or the thread did not start). `host_install_begin` answers through the poll rather than returning
-1 when it cannot ask (it returns -1 only while an install is pending, which the page ignores).
The drivers folder route needs no picker: `<files>/drivers/` (`IcoAndroidPaths.drivers`) holds
zips the player copied by USB; `ico_gpu_driver_list_zips` lists the `*.zip` files (any case, no
`incoming.*`, depth 0), the Graphics driver page shows the row `UI_OPT_GPU_ADD_FILE` while
`fileCount()` is above 0, and `installFileBegin(i)` runs the picker's own copy thread on the
zip's path (`SDL_IOFromFile` takes a plain path) and deletes the zip after `ADDED`. `<files>`
is the internal folder when shared storage is not writable, so the folder can be the install
root itself; the listing skips everything but top-level zips, so that is harmless. The
start-up box (`ico_gpu_driver_android_choose`) falls back to the first zip there on `NO_PICKER`,
`NO_PICKER_APP` and `CANCELLED` (some ROMs answer `RESULT_CANCELED` at once, issue 135); with no zip
there its sentence names the folder. The popups, the box and the Graphics driver row's note name
the folder through `ico_gpu_driver_android_folder_text` (the start-up box's text is
`ico_gpu_driver_android_box_text`; `window_host.c` is at its size cap, so the Android wording lives here) (`UiGpuDriverHost.folderPath`;
`IcoAndroidPaths.drivers` without the internal storage root `/storage/emulated/<n>/`, as the phone's
file apps show it), as a `%s` in the table strings (`NO_PICKER`, `NO_PICKER_APP`, `CANCELLED`,
`NOTE_LOST`), put in by `settings.c` `fmtText` and wrapped (`UI_POPUP_TEXT` is 384 bytes).

Page size check. `install_thread` passes `getpagesize()` to `ico_gpu_driver_install_zip_pages`:
on a phone whose page is 16384 bytes or more, every file of the zip that ends in `.so` is read after it is
unpacked into the staging folder (first 64 KB) and `ico_gpu_driver_elf_pages_ok` reads its ELF header and
program header table (little-endian ELF32/ELF64); a PT_LOAD with `p_align` under the page size refuses the
package with `ICO_GPU_DRIVER_PAGES_MESSAGE` and the staging folder goes. What cannot be read (not ELF, a table
past the 64 KB, a 4 KB phone) is never refused. How Android treats a 4 KB-aligned driver loaded on 16 KB
pages is not verified here (the research report marks it unverified): the check follows the library's
declared alignment, which is what a 16 KB phone's loader needs. Log line: `gpu driver: the chosen file was
not installed (memory page size N): <why>`.

Last-seen lines. `ico_gpu_driver_android_device_up` writes what the device says of its driver (`RhiLimits`
`driverName`, `driverVersion`, `driverId`, `apiMajor`/`apiMinor`, `mesa`) as `last_seen.json` in the running
package's folder, or as `last_seen_system.json` in the drivers root for the phone's own driver
(`ico_gpu_driver_seen_write`; the name `last_seen_system.json` is a reserved folder name), and logs `gpu driver:
this start runs <folder|the phone's own driver>: <words> (driver ID N), <Mesa version>`. The Graphics driver
page's `info(i)` reads them (cached until the list or a start changes them) as `ico_gpu_driver_seen_text`
words: "Qualcomm's driver 512.676.1, Vulkan 1.3" (driver id 8, or a name with "qualcomm"), "Turnip, Mesa
25.3.0-devel, Vulkan 1.4" (driver id 18 or "turnip"), else the name, its Mesa version or version, the Vulkan
version and "driver ID n"; a driver never started says so. The page puts the chosen driver's line at the
head of its note (English, as the driver names are; `settings.c` `gpuRefresh`), and drops it when it and the
note would not fit the note's buffer.

Loss in the first 30 s. `window_host.c` calls `ico_gpu_driver_android_device_lost` from `device_lost_quit`
and from `window_gpu_hang` (the hang is treated as a loss): under a package, a loss within
`ICO_GPU_DRIVER_LOSS_WINDOW_MS` (30 s) of the first present (`ico_gpu_driver_android_presented` stamps it) or
before any writes the trial marker again (it went after 300 presents), so the next start falls back to the
phone's own driver with the box like a crash inside the driver. Logged either way, once a run: `gpu driver:
the graphics device was lost under <folder> (after the first present, N ms; inside the 30000 ms that count
like a crash in the driver): the next start falls back ...`, or `... past the 30000 ms that count against
the driver; the next start keeps it`.

A pick that never answers ends as `CANCELLED` (`pick_watch`, called by `host_install_poll` and
`install_busy`): `IcoActivity.onResume` counts into `ico_android_resume_count`
(`nativeResumed`), and 2 s after a resume that came with no answer (an answer arrives before the
resume), or 120 s after the start, the wait is over; the late answer finds the state moved on.
Log lines of this: `the app came back to the front N ms after the picker opened (pick N); no
answer yet`, `the file picker (pick N) gave no answer: <why> (N ms); the wait is over`, and
`closed without a file, N ms after it opened`; `ico_pc: file picker: pick N: onActivityResult
resultCode <n> (OK|CANCELED), data null|present, a file|no file` from the Java side. Log lines, all `gpu driver:` on stderr: `Add a driver: opening the file
picker (pick N)`, `Add from the drivers folder: <name> (pick N)`, `the file picker answered
(pick N): content://<authority>/...` or `closed without a file, N ms after it opened`, `the file picker failed:
<why>`, `a late answer for pick N (now M) ignored`, `the file picker could not open: <why>`, `Add a
driver ended: ADDED|NOT A PACKAGE|NO SPACE|NO PICKER|NO PICKER APP|UNREADABLE|CANCELLED`, and the
install thread's own. After a device loss on the phone's own Qualcomm driver (no package loaded;
`ico_gpu_driver_android_device_up`, called by `window_host.c` after `rd_init`: `vendorId` Qualcomm and
`ICO_DIAG_END_LOSS` above 0),
`gpu driver: the previous start lost the graphics device on this phone's own Qualcomm driver; a
Turnip driver package ... is loaded by the game itself, without the system's driver` is logged
once and the row's note becomes `UI_STR_GPU_DRIVER_NOTE_LOST` (a failed driver's note still wins).
When no device starts, `window: a graphics driver package (...) is loaded by the game itself,
without the system's Vulkan loader` precedes the box.

`TU_DEBUG` before a Turnip package loads (issue 123's ladder, step 2; [VU_SKIN_CHECK.md](VU_SKIN_CHECK.md#the-ladder)).
`tu_debug_apply`, called by `ico_gpu_driver_android_start` before `adrenotools_open_libvulkan`,
reads `[dev] turnip_debug` ([CONFIG.md](CONFIG.md)): `nobin`, which the skin check writes when the
twins of a skinned draw still differ with the bones read through the ring, sets `TU_DEBUG=nobin` in
the process environment (appended to a value already set), only when the package's library is
Turnip's (`libvulkan_freedreno.so`, `library_is_turnip`): `gpu driver: TU_DEBUG=nobin set before
<library> loads ([dev] turnip_debug = nobin, turnip_debug_by = <mark>): whether the driver reads it
through the package loader is not verified; the skin check's twins under it are the indicator`, or
`gpu driver: [dev] turnip_debug = nobin, but the package's library is <library>, not Turnip's
libvulkan_freedreno.so: TU_DEBUG is not set`. Mesa's documentation lists `nobin` among `TU_DEBUG`'s
options (docs.mesa3d.org/drivers/freedreno.html). It is unverified that the variable reaches the
driver on Android through libadrenotools' loader: the twins' result under it is the indicator
(`window_bones.c` reads `TU_DEBUG` back and says `window: VU skin check ladder: step 2 of 2
(TU_DEBUG=nobin) for this start; ...`; twins still differing write `turnip_debug = off`).
`ico_gpu_driver_android_turnip_package` tells the ladder whether this start runs such a package
(the start the variable can reach).

### The file picker

Android has the app's own file picker, not SDL's (issue 135). SDL's `SDL_ShowOpenFileDialog`
sends one `ACTION_OPEN_DOCUMENT` intent and keeps one pending dialog in
`SDLActivity.mFileDialogState`, which is `private static` and cleared only by that dialog's
answer, so the app cannot clear a dialog that never answered, and a device without a documents
app fails every pick with a JNI error. SDL's Java is fetched verbatim (`tools/fetch_android.sh`)
and never edited, so the picker lives in `IcoActivity`:
`ico_android_pick_file(gen, fn, user, &noApp, why, n)` (`port/platform/android/host_android.h`)
stores the pending pick (number, callback, user) under a spin lock and calls the static
`IcoActivity.pickFile(int gen)` on the game's thread (as SDL calls its own `showFileDialog`).
`pickFile` logs the activity's state (`SDLActivity.mIsResumedCalled`, `mHasFocus`,
`mCurrentNativeState`) and the app each intent resolves to, then starts `ACTION_OPEN_DOCUMENT`
(`CATEGORY_OPENABLE`, type `*/*`) with request code `0x1C0D`; on `ActivityNotFoundException`
it starts `ACTION_GET_CONTENT` the same way. It answers `null` when a picker started, `no app
for OPEN_DOCUMENT (<e>) nor GET_CONTENT (<e>)` when neither intent has an app (the C side reads
the `no app` prefix as `*noApp = 1`), or the exception's text. A failed start clears the pending
pick and returns -1. `IcoActivity.onActivityResult` takes request `0x1C0D` and passes every other
code to SDL; it calls `nativePickDone(gen, uri, why)` on the activity's thread with the chosen
URI (`getData()`, else the first clip item), `uri` and `why` both null for a picker closed
without a file, or `why` set when `RESULT_OK` came without a file. Nothing is persisted: the
copy runs while the grant lasts. `nativePickDone` copies the strings, takes and clears the
pending pick under the lock before calling its callback (so the callback may start the next
pick), and drops an answer whose number is not the pending pick's. The callbacks
(`gpu_driver_android.c` `pick_done`, `main_host.c` `pick_answer`) run on the activity's thread
and touch only atomics, the copied address and `SDL_CreateThread`, as SDL's answer callback did.
The manifest's `<queries>` names both intents, so on Android 11 and later the log can name the
app they resolve to (the start itself does not need it). The first start's disc picker
(`main_host.c` `pick_iso_sdl` on Android) uses the same bridge, so it gets the `GET_CONTENT`
fallback too; Back while it waits gives up through the pick's number. Log lines: `ico_pc: file
picker: pick N: the activity is resumed|not resumed, focused|not focused, native state
INIT|RESUMED|PAUSED`, `ico_pc: file picker: pick N: OPEN_DOCUMENT goes to <component>|no app`,
`ico_pc: file picker: pick N: no app for OPEN_DOCUMENT (<e>); trying GET_CONTENT`, `ico_pc: file
picker: pick N: GET_CONTENT goes to <component>|no app`, `ico_pc: file picker: a late answer for
pick N (now M) ignored`, and for the disc picker `ico_pc: the file picker could not open: <why>`
and `ico_pc: the file picker failed: <why>`. One limit: Android gives every answer the same
request code, so if an earlier picker that the game gave up on answers after a new pick started,
that answer is taken as the new pick's (the game only gives up on a picker that did not appear).

Options > Extras > Back up your data uses the same pending pick for the system's "save as" screen:
`ico_android_create_file(gen, name, mime, fn, user, &noApp, why, n)` calls the static
`IcoActivity.createFile(int gen, String name, String mime)`, which starts `ACTION_CREATE_DOCUMENT`
(`CATEGORY_OPENABLE`, the type, `EXTRA_TITLE` the name) with request code `0x1C0E`;
`onActivityResult` takes both codes and answers through `nativePickDone` (its log line says
`create N:` for this one). `IcoActivity.openFd(String uri, String mode)` opens a URI either screen
answered through the content resolver and hands the detached descriptor to the native side
(`ico_android_open_fd`; a failure logs `ico_pc: file picker: open <mode> failed: <exception
kind>`, never the exception's text, which can name the player's file), and
`IcoActivity.displayName(String uri)` gives the name the system shows for it
(`ico_android_display_name`). The manifest's `<queries>` names `CREATE_DOCUMENT` too
([Backups](PLATFORM.md#backups)).

